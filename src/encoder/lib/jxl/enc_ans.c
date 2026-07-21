// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_ans.h"

#include <jxl/memory_manager.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/base/bits.h"
#include "lib/jxl/base/array.h"
#include "lib/jxl/base/compiler_specific.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_ans_simd.h"
#include "lib/jxl/layer_type.h"
#include "lib/jxl/enc_cluster.h"
#include "lib/jxl/enc_context_map.h"
#include "lib/jxl/enc_fields.h"
#include "lib/jxl/enc_huffman.h"
#include "lib/jxl/enc_lz77.h"
#include "lib/jxl/fields.h"
#include "lib/jxl/memory_manager_internal.h"
#include "lib/jxl/simd_util.h"
#include "lib/jxl/base/common.h"


typedef struct jxl_entropy_delta {
  jxl_ans_hist_bin freq;   // initial count
  size_t count_ind;  // index of current bin value in `allowed_counts`
  size_t bin_ind;    // index of current bin in `counts`
} jxl_entropy_delta;
JXL_DEFINE_POD_ARRAY(jxl_array_entropy_delta, jxl_entropy_delta)

enum { kMaxNumSymbolsForSmallCode = 2 };

static void jxl_store_var_len_uint8(size_t n, jxl_bit_sink* sink) {
  JXL_DASSERT(n <= 255);
  if (n == 0) {
    jxl_bit_sink_write(sink, 1, 0);
  } else {
    jxl_bit_sink_write(sink, 1, 1);
    size_t nbits = jxl_floor_log2_nonzero32(n);
    jxl_bit_sink_write(sink, 3, nbits);
    jxl_bit_sink_write(sink, nbits, n - (1ULL << nbits));
  }
}

static void jxl_store_var_len_uint16(size_t n, jxl_bit_sink* sink) {
  JXL_DASSERT(n <= 65535);
  if (n == 0) {
    jxl_bit_sink_write(sink, 1, 0);
  } else {
    jxl_bit_sink_write(sink, 1, 1);
    size_t nbits = jxl_floor_log2_nonzero32(n);
    jxl_bit_sink_write(sink, 4, nbits);
    jxl_bit_sink_write(sink, nbits, n - (1ULL << nbits));
  }
}

typedef struct jxl_ans_encoding_histogram jxl_ans_encoding_histogram;
static void jxl_ans_encoding_histogram_swap(jxl_ans_encoding_histogram* self,
                              jxl_ans_encoding_histogram* other);

static const jxl_array_i32* jxl_ans_encoding_histogram_counts(const jxl_ans_encoding_histogram* self);
static float jxl_ans_encoding_histogram_cost(const jxl_ans_encoding_histogram* self);
static jxl_status jxl_ans_encoding_histogram_encode(jxl_ans_encoding_histogram* self, jxl_bit_sink* sink);
static jxl_status jxl_ans_encoding_histogram_compute_best(
    const jxl_histogram* histo, const jxl_array_i32* counts,
    jxl_ans_histogram_strategy ans_histogram_strategy,
    jxl_ans_encoding_histogram* out);
static void jxl_ans_encoding_histogram_ans_build_info_table(
    jxl_ans_encoding_histogram* self, const jxl_alias_table_entry* table,
    size_t log_alpha_size, jxl_ans_enc_symbol_info* info, jxl_array_u16* reverse_maps);
static jxl_status jxl_ans_encoding_histogram_try_ans_histogram_shift(
    uint32_t shift, jxl_ans_encoding_histogram* normalized,
    jxl_ans_encoding_histogram* result, const jxl_histogram* histo,
    const jxl_array_i32* counts);
static float jxl_ans_encoding_histogram_estimate_data_bits(const jxl_ans_encoding_histogram* self,
                                           const jxl_histogram* histo,
                                           const jxl_array_i32* counts);
static float jxl_ans_encoding_histogram_estimate_data_bits_flat(const jxl_histogram* histo,
                                               const jxl_array_i32* counts);
static uint32_t jxl_ans_encoding_histogram_smallest_increment_log(uint32_t count,
                                                  uint32_t shift);
static bool jxl_ans_encoding_histogram_rebalance_histogram(jxl_ans_encoding_histogram* self,
                                            const jxl_histogram* histo,
                                            const jxl_array_i32* counts);

// Fixed-point log2 LUT for values of [0,4096]
typedef struct jxl_ans_encoding_histogram_lg2_lut {
  uint32_t v[ANS_TAB_SIZE + 1];
} jxl_ans_encoding_histogram_lg2_lut;
static const size_t kANSEncodingHistogramLg2LUTSize = ANS_TAB_SIZE + 1;

static inline uint32_t* jxl_ans_encoding_histogram_lg2_lut_at(jxl_ans_encoding_histogram_lg2_lut* self,
                                              size_t i) {
  return &self->v[i];
}
static inline const uint32_t* jxl_ans_encoding_histogram_lg2_lut_at_const(
    const jxl_ans_encoding_histogram_lg2_lut* self, size_t i) {
  return &self->v[i];
}

typedef struct jxl_ans_encoding_histogram_counts_entropy {
  jxl_ans_hist_bin count : 16;     // allowed value of counts in a histogram bin
  jxl_ans_hist_bin step_log : 16;  // log2 of increase step size (can use 5 bits)
  int32_t delta_lg2;  // change of log between that value and the next allowed
} jxl_ans_encoding_histogram_counts_entropy;

// Array is sorted by decreasing allowed counts for each possible shift.
// Exclusion of single-bin histograms before `RebalanceHistogram` allows
// to put count upper limit of 4095, and shifts of 11 and 12 produce the
// same table
typedef struct jxl_ans_encoding_histogram_counts_array {
  jxl_ans_encoding_histogram_counts_entropy rows[ANS_LOG_TAB_SIZE][ANS_TAB_SIZE];
} jxl_ans_encoding_histogram_counts_array;
static const size_t kANSEncodingHistogramCountsArraySize = ANS_LOG_TAB_SIZE;

static inline jxl_ans_encoding_histogram_counts_entropy* jxl_ans_encoding_histogram_counts_array_at(
    jxl_ans_encoding_histogram_counts_array* self, size_t i) {
  return self->rows[i];
}
static inline const jxl_ans_encoding_histogram_counts_entropy*
jxl_ans_encoding_histogram_counts_array_at_const(
    const jxl_ans_encoding_histogram_counts_array* self, size_t i) {
  return self->rows[i];
}

typedef struct jxl_ans_encoding_histogram_counts_index {
  uint16_t rows[ANS_LOG_TAB_SIZE][ANS_TAB_SIZE];
} jxl_ans_encoding_histogram_counts_index;

static inline uint16_t* jxl_ans_encoding_histogram_counts_index_at(
    jxl_ans_encoding_histogram_counts_index* self, size_t i) {
  return self->rows[i];
}
static inline const uint16_t* jxl_ans_encoding_histogram_counts_index_at_const(
    const jxl_ans_encoding_histogram_counts_index* self, size_t i) {
  return self->rows[i];
}

typedef struct jxl_ans_encoding_histogram_allowed_counts {
  jxl_ans_encoding_histogram_counts_array array;
  jxl_ans_encoding_histogram_counts_index index;
} jxl_ans_encoding_histogram_allowed_counts;

struct jxl_ans_encoding_histogram {
  // Returns the difference between largest count that can be represented and is
  // smaller than "count" and smallest representable count larger than "count".

  float cost_;
  uint32_t method_;
  size_t omit_pos_;
  size_t alphabet_size_;
  size_t num_symbols_;
  size_t symbols_[kMaxNumSymbolsForSmallCode];
  jxl_array_i32 counts_;

};

static inline void jxl_ans_encoding_histogram_construct_empty(
    jxl_ans_encoding_histogram* self, jxl_memory_manager* mm) {
  self->cost_ = 0;
  self->method_ = 0;
  self->omit_pos_ = 0;
  self->alphabet_size_ = 0;
  self->num_symbols_ = 0;
  memset(self->symbols_, 0, sizeof(self->symbols_));
  jxl_array_construct_empty(&self->counts_, mm);
}

static jxl_ans_encoding_histogram_lg2_lut g_ans_encoding_histogram_lg2;
static int g_ans_encoding_histogram_lg2_ready = 0;

static void jxl_ans_encoding_histogram_lg2_lut_init(jxl_ans_encoding_histogram_lg2_lut* lg2) {
  *jxl_ans_encoding_histogram_lg2_lut_at(lg2, 0) = 0;  // for entropy calculations it is OK
  for (size_t i = 1; i < kANSEncodingHistogramLg2LUTSize; ++i) {
    *jxl_ans_encoding_histogram_lg2_lut_at(lg2, i) =
        round(ldexp(log2(i) / ANS_LOG_TAB_SIZE, 31));
  }
}

static const jxl_ans_encoding_histogram_lg2_lut* jxl_ans_encoding_histogram_lg2(void) {
  if (!g_ans_encoding_histogram_lg2_ready) {
    jxl_ans_encoding_histogram_lg2_lut_init(&g_ans_encoding_histogram_lg2);
    g_ans_encoding_histogram_lg2_ready = 1;
  }
  return &g_ans_encoding_histogram_lg2;
}

static jxl_ans_encoding_histogram_allowed_counts g_ans_encoding_histogram_allowed_counts;
static int g_ans_encoding_histogram_allowed_counts_ready = 0;

static void jxl_ans_encoding_histogram_allowed_counts_init(
    jxl_ans_encoding_histogram_allowed_counts* result) {
  for (uint32_t shift = 0; shift < kANSEncodingHistogramCountsArraySize;
       ++shift) {
    jxl_ans_encoding_histogram_counts_entropy* ac =
        jxl_ans_encoding_histogram_counts_array_at(&result->array, shift);
    uint16_t* ai = jxl_ans_encoding_histogram_counts_index_at(&result->index, shift);
    jxl_ans_hist_bin last = ~0;
    size_t slot = 0;
    // TODO(eustas): are those "default" values relevant?
    ac[0].delta_lg2 = 0;
    ac[0].step_log = 0;
    for (int32_t i = (int32_t)(ANS_TAB_SIZE) - 1; i >= 0; --i) {
      int32_t curr =
          i & ~((1 << jxl_ans_encoding_histogram_smallest_increment_log(i, shift)) - 1);
      if (curr == last) continue;
      last = curr;
      ac[slot].count = curr;
      ai[curr] = slot;
      if (curr == 0) {
        // Guards against non-possible steps:
        // at max value [0] - 0 (by init), at min value - max
        ac[slot].delta_lg2 = INT32_MAX;
        ac[slot].step_log = 0;
      } else if (slot > 0) {
        jxl_ans_hist_bin prev = ac[slot - 1].count;
        ac[slot].delta_lg2 = round(
            ldexp(log2((double)(prev) / curr) / ANS_LOG_TAB_SIZE, 31));
        ac[slot].step_log = jxl_floor_log2_nonzero32((uint32_t)(prev - curr));
        prev = curr;
      }
      slot++;
    }
  }
}

static const jxl_ans_encoding_histogram_allowed_counts*
jxl_ans_encoding_histogram_allowed_counts_table(void) {
  if (!g_ans_encoding_histogram_allowed_counts_ready) {
    jxl_ans_encoding_histogram_allowed_counts_init(
        &g_ans_encoding_histogram_allowed_counts);
    g_ans_encoding_histogram_allowed_counts_ready = 1;
  }
  return &g_ans_encoding_histogram_allowed_counts;
}

static inline jxl_status jxl_ans_encoding_histogram_copy_from(jxl_ans_encoding_histogram* self,
                                           const jxl_ans_encoding_histogram* other) {
  if (self == other) return jxl_ok_status();
  self->cost_ = other->cost_;
  self->method_ = other->method_;
  self->omit_pos_ = other->omit_pos_;
  self->alphabet_size_ = other->alphabet_size_;
  self->num_symbols_ = other->num_symbols_;
  for (size_t i = 0; i < kMaxNumSymbolsForSmallCode; ++i) {
    self->symbols_[i] = other->symbols_[i];
  }
  return jxl_array_copy_from(&self->counts_, &other->counts_);
}

static inline void jxl_ans_encoding_histogram_swap(jxl_ans_encoding_histogram* self,
                                     jxl_ans_encoding_histogram* other) {
  float tc = self->cost_;
  self->cost_ = other->cost_;
  other->cost_ = tc;
  uint32_t tm = self->method_;
  self->method_ = other->method_;
  other->method_ = tm;
  size_t to = self->omit_pos_;
  self->omit_pos_ = other->omit_pos_;
  other->omit_pos_ = to;
  size_t ta = self->alphabet_size_;
  self->alphabet_size_ = other->alphabet_size_;
  other->alphabet_size_ = ta;
  size_t tn = self->num_symbols_;
  self->num_symbols_ = other->num_symbols_;
  other->num_symbols_ = tn;
  for (size_t i = 0; i < kMaxNumSymbolsForSmallCode; ++i) {
    size_t ts = self->symbols_[i];
    self->symbols_[i] = other->symbols_[i];
    other->symbols_[i] = ts;
  }
  jxl_array_swap(&self->counts_, &other->counts_);
}

static inline const jxl_array_i32* jxl_ans_encoding_histogram_counts(const jxl_ans_encoding_histogram* self) {
  return &self->counts_;
}
static inline float jxl_ans_encoding_histogram_cost(const jxl_ans_encoding_histogram* self) {
  return self->cost_;
}

static jxl_status jxl_ans_encoding_histogram_encode(jxl_ans_encoding_histogram* self, jxl_bit_sink* sink) {
    // The check ensures also that all RLE sequences can be
    // encoded by `jxl_store_var_len_uint8`
    JXL_ENSURE(self->alphabet_size_ <= ANS_MAX_ALPHABET_SIZE);

    /// Flat histogram.
    if (self->method_ == 0) {
      // Mark non-small tree.
      jxl_bit_sink_write(sink, 1, 0);
      // Mark uniform histogram.
      jxl_bit_sink_write(sink, 1, 1);
      JXL_ENSURE(self->alphabet_size_ > 0);
      // Encode alphabet size.
      jxl_store_var_len_uint8(self->alphabet_size_ - 1, sink);

      return jxl_ok_status();
    }

    /// Small tree.
    if (self->num_symbols_ <= kMaxNumSymbolsForSmallCode) {
      // Small tree marker to encode 1-2 symbols.
      jxl_bit_sink_write(sink, 1, 1);
      if (self->num_symbols_ == 0) {
        jxl_bit_sink_write(sink, 1, 0);
        jxl_store_var_len_uint8(0, sink);
      } else {
        jxl_bit_sink_write(sink, 1, self->num_symbols_ - 1);
        for (size_t i = 0; i < self->num_symbols_; ++i) {
          jxl_store_var_len_uint8(self->symbols_[i], sink);
        }
      }
      if (self->num_symbols_ == 2) {
        jxl_bit_sink_write(sink, ANS_LOG_TAB_SIZE, *jxl_array_at(&self->counts_, self->symbols_[0]));
      }

      return jxl_ok_status();
    }

    /// General tree.
    // Mark non-small tree.
    jxl_bit_sink_write(sink, 1, 0);
    // Mark non-flat histogram.
    jxl_bit_sink_write(sink, 1, 0);

    // Elias gamma-like code for `shift = method - 1`. Only difference is that
    // if the number of bits to be encoded is equal to `upper_bound_log`,
    // we skip the terminating 0 in unary coding.
    int upper_bound_log =
        jxl_floor_log2_nonzero32((uint32_t)(ANS_LOG_TAB_SIZE + 1));
    int log = jxl_floor_log2_nonzero32((uint32_t)(self->method_));
    jxl_bit_sink_write(sink, log, (1 << log) - 1);
    if (log != upper_bound_log) jxl_bit_sink_write(sink, 1, 0);
    jxl_bit_sink_write(sink, log, ((1 << log) - 1) & self->method_);

    // Since `self->num_symbols_ >= 3`, we know that `self->alphabet_size_ >= 3`, therefore
    // we encode `self->alphabet_size_ - 3`.
    jxl_store_var_len_uint8(self->alphabet_size_ - 3, sink);

    // Precompute sequences for RLE encoding. Contains the number of identical
    // values starting at a given index. Only contains that value at the first
    // element of the series.
    uint8_t same[ANS_MAX_ALPHABET_SIZE];
    memset(same, 0, sizeof(same));
    size_t last = 0;
    for (size_t i = 1; i <= self->alphabet_size_; i++) {
      // Store the sequence length once different symbol reached, or we are
      // near the self->omit_pos_, or we're at the end. We don't support including the
      // self->omit_pos_ in an RLE sequence because this value may use a different
      // amount of log2 bits than standard, it is too complex to handle in the
      // decoder.
      if (i == self->alphabet_size_ || i == self->omit_pos_ || i == self->omit_pos_ + 1 ||
          *jxl_array_at(&self->counts_, i) != *jxl_array_at(&self->counts_, last)) {
        same[last] = i - last;
        last = i;
      }
    }

    uint8_t bit_width[ANS_MAX_ALPHABET_SIZE];
    memset(bit_width, 0, sizeof(bit_width));
    // Use shortest possible Huffman code to encode `omit_pos` (see
    // `kBitWidthLengths`). `bit_width` value at `omit_pos` should be the
    // first of maximal values in the whole `bit_width` array, so it can be
    // increased without changing that property
    int omit_width = 10;
    for (size_t i = 0; i < self->alphabet_size_; ++i) {
      if (i != self->omit_pos_ && *jxl_array_at(&self->counts_, i) > 0) {
        bit_width[i] = jxl_floor_log2_nonzero32((uint32_t)(*jxl_array_at(&self->counts_, i))) + 1;
        omit_width = JXL_MAX(omit_width, bit_width[i] + (int)(i < self->omit_pos_));
      }
    }
    bit_width[self->omit_pos_] = (uint8_t)(omit_width);

    // The bit widths are encoded with a static Huffman code.
    // The last symbol is used as RLE sequence.
    const uint8_t kBitWidthLengths[ANS_LOG_TAB_SIZE + 2] = {
        5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 6, 7, 7,
    };
    const uint8_t kBitWidthSymbols[ANS_LOG_TAB_SIZE + 2] = {
        17, 11, 15, 3, 9, 7, 4, 2, 5, 6, 0, 33, 1, 65,
    };
    const uint8_t kMinReps = 5;
    const size_t rep = ANS_LOG_TAB_SIZE + 1;
    // Encode count bit widths
    for (size_t i = 0; i < self->alphabet_size_; ++i) {
      jxl_bit_sink_write(sink, kBitWidthLengths[bit_width[i]],
                   kBitWidthSymbols[bit_width[i]]);
      if (same[i] >= kMinReps) {
        // Encode the RLE symbol and skip the repeated ones.
        jxl_bit_sink_write(sink, kBitWidthLengths[rep], kBitWidthSymbols[rep]);
        jxl_store_var_len_uint8(same[i] - kMinReps, sink);
        i += same[i] - 1;
      }
    }
    // Encode additional bits of accuracy
    uint32_t shift = self->method_ - 1;
    if (shift != 0) {  // otherwise `bitcount = 0`
      for (size_t i = 0; i < self->alphabet_size_; ++i) {
        if (bit_width[i] > 1 && i != self->omit_pos_) {
          int bitcount = jxl_get_population_count_precision(bit_width[i] - 1, shift);
          int drop_bits = bit_width[i] - 1 - bitcount;
          JXL_DASSERT((*jxl_array_at(&self->counts_, i) & ((1 << drop_bits) - 1)) == 0);
          jxl_bit_sink_write(sink, bitcount,
                       (*jxl_array_at(&self->counts_, i) >> drop_bits) - (1 << bitcount));
        }
        if (same[i] >= kMinReps) {
          // Skip symbols encoded by RLE.
          i += same[i] - 1;
        }
      }
    }
    return jxl_ok_status();
  }

static void jxl_ans_encoding_histogram_ans_build_info_table(jxl_ans_encoding_histogram* self, const jxl_alias_table_entry* table, size_t log_alpha_size, jxl_ans_enc_symbol_info* info, jxl_array_u16* reverse_maps) {
    // Create valid alias table for empty streams. Append into the flat
    // reverse-map buffer with global offsets.
    const size_t base = jxl_array_len(reverse_maps);
    size_t map_total = 0;
    for (size_t s = 0; s < JXL_MAX((size_t)(1), self->alphabet_size_); ++s) {
      const jxl_ans_hist_bin freq = s == self->alphabet_size_ ? ANS_TAB_SIZE : *jxl_array_at(&self->counts_, s);
      info[s].freq_ = (uint16_t)(freq);
#ifdef USE_MULT_BY_RECIPROCAL
      if (freq != 0) {
        info[s].ifreq_ = ((1ull << RECIPROCAL_PRECISION) + info[s].freq_ - 1) /
                         info[s].freq_;
      } else {
        info[s].ifreq_ =
            1;  // Shouldn't matter (symbol shouldn't occur), but...
      }
#endif
      info[s].reverse_map_offset = (uint32_t)(base + map_total);
      map_total += (size_t)(freq);
    }
    if (!jxl_status_ok(jxl_array_resize_zero(reverse_maps, base + map_total))) {
      // ANS table build has no jxl_status on this path; OOM is fatal.
      JXL_CRASH();
    }
    size_t log_entry_size = ANS_LOG_TAB_SIZE - log_alpha_size;
    size_t entry_size_minus_1 = (1 << log_entry_size) - 1;
    for (int i = 0; i < ANS_TAB_SIZE; i++) {
      jxl_alias_table_symbol s =
          jxl_alias_table_lookup(table, i, log_entry_size, entry_size_minus_1);
      *jxl_array_at(reverse_maps, info[s.value].reverse_map_offset + s.offset) =
          (uint16_t)(i);
    }
  }

static jxl_status jxl_ans_encoding_histogram_try_ans_histogram_shift(uint32_t shift, jxl_ans_encoding_histogram* normalized, jxl_ans_encoding_histogram* result, const jxl_histogram* histo, const jxl_array_i32* counts) {
    normalized->method_ = JXL_MIN(shift, ANS_LOG_TAB_SIZE - 1) + 1;

    if (!jxl_ans_encoding_histogram_rebalance_histogram(normalized, histo, counts)) {
      return JXL_FAILURE("Logic error: couldn't rebalance a histogram");
    }
    jxl_size_writer writer;
    jxl_size_writer_construct_empty(&writer);
    jxl_bit_sink sink = jxl_make_size_writer_bit_sink(&writer);
    JXL_RETURN_IF_ERROR(jxl_ans_encoding_histogram_encode(normalized, &sink));
    normalized->cost_ =
        writer.size + jxl_ans_encoding_histogram_estimate_data_bits(normalized, histo, counts);
    if (normalized->cost_ < result->cost_) {
      JXL_RETURN_IF_ERROR(jxl_ans_encoding_histogram_copy_from(result, normalized));
    }
    return jxl_ok_status();
  }

static float jxl_ans_encoding_histogram_estimate_data_bits(const jxl_ans_encoding_histogram* self, const jxl_histogram* histo, const jxl_array_i32* counts) {
    int64_t sum = 0;
    for (size_t i = 0; i < self->alphabet_size_; ++i) {
      // += histogram[i] * -log(counts[i]/total_counts)
      sum += *jxl_array_at_const(counts, i) * (int64_t)(*jxl_ans_encoding_histogram_lg2_lut_at_const(jxl_ans_encoding_histogram_lg2(), *jxl_array_at_const(&self->counts_, i)));
    }
    return (histo->total_count - ldexpf(sum, -31)) * ANS_LOG_TAB_SIZE;
  }

static float jxl_ans_encoding_histogram_estimate_data_bits_flat(const jxl_histogram* histo, const jxl_array_i32* counts) {
    size_t len = jxl_histogram_alphabet_size(counts);
    int64_t flat_bits = (int64_t)(*jxl_ans_encoding_histogram_lg2_lut_at_const(jxl_ans_encoding_histogram_lg2(), len)) * ANS_LOG_TAB_SIZE;
    return ldexpf(histo->total_count * flat_bits, -31);
  }

static uint32_t jxl_ans_encoding_histogram_smallest_increment_log(uint32_t count, uint32_t shift) {
    if (count == 0) return 0;
    uint32_t bits = jxl_floor_log2_nonzero32(count);
    uint32_t drop_bits = bits - jxl_get_population_count_precision(bits, shift);
    return drop_bits;
  }

static bool jxl_ans_encoding_histogram_rebalance_histogram(jxl_ans_encoding_histogram* self, const jxl_histogram* histo, const jxl_array_i32* counts) {
    const jxl_ans_hist_bin table_size = ANS_TAB_SIZE;
    uint32_t shift = self->method_ - 1;

    // Penalties corresponding to different step sizes - entropy decrease in
    // balancing bin, step of size (1 << ANS_LOG_TAB_SIZE - 1) is not possible
    int64_t balance_inc[ANS_LOG_TAB_SIZE - 1];
    int64_t balance_dec[ANS_LOG_TAB_SIZE - 1];
    memset(balance_inc, 0, sizeof(balance_inc));
    memset(balance_dec, 0, sizeof(balance_dec));
    const jxl_ans_encoding_histogram_counts_entropy* ac = jxl_ans_encoding_histogram_counts_array_at_const(&jxl_ans_encoding_histogram_allowed_counts_table()->array, shift);
    const uint16_t* ai = jxl_ans_encoding_histogram_counts_index_at_const(&jxl_ans_encoding_histogram_allowed_counts_table()->index, shift);
    // TODO(ivan) separate cases of shift >= 11 - all steps are 1 there, and
    // possibly 10 - all relevant steps are 2.
    // Total entropy change by a step: increase/decrease in current bin
    // together with corresponding decrease/increase in the balancing bin.
    // Inc steps increase current bin, dec steps decrease.
    // Vector of adjustable bins from `jxl_ans_encoding_histogram_allowed_counts_table`
    jxl_array_entropy_delta bins;
    jxl_array_construct_empty(&bins, self->counts_.memory_manager);
if (!jxl_status_ok(jxl_array_reserve(&bins, 256))) JXL_CRASH();
    double norm = (double)(table_size) / histo->total_count;

    size_t remainder_pos = 0;  // highest balancing bin in the histogram
    int64_t max_freq = 0;
    jxl_ans_hist_bin rest = table_size;  // reserve of histogram counts to distribute
    for (size_t n = 0; n < self->alphabet_size_; ++n) {
      jxl_ans_hist_bin freq = *jxl_array_at_const(counts, n);
      if (freq > max_freq) {
        remainder_pos = n;
        max_freq = freq;
      }

      double target = freq * norm;  // rounding
      // Keep zeros and clamp nonzero freq counts to [1, table_size)
      jxl_ans_hist_bin count = JXL_MAX((jxl_ans_hist_bin)(round(target)),
                                 (jxl_ans_hist_bin)(freq > 0));
      count = JXL_MIN(count, (jxl_ans_hist_bin)(table_size - 1));
      uint32_t step_log = jxl_ans_encoding_histogram_smallest_increment_log(count, shift);
      jxl_ans_hist_bin inc = 1 << step_log;
      count &= ~(inc - 1);

      *jxl_array_at(&self->counts_, n) = count;
      rest -= count;
      if (target > 1.0) {
        jxl_entropy_delta delta;
        delta.freq = freq;
        delta.count_ind = ai[count];
        delta.bin_ind = n;
        if (!jxl_status_ok(jxl_array_entropy_delta_push_back(&bins, delta))) JXL_CRASH();
      }
    }

    // Delete the highest balancing bin from adjustable by `jxl_ans_encoding_histogram_allowed_counts_table`
    {
      size_t idx = jxl_array_len(&bins);
      for (size_t i = 0; i < jxl_array_len(&bins); ++i) {
        if (jxl_array_at(&bins, i)->bin_ind == remainder_pos) {
          idx = i;
          break;
        }
      }
      if (idx < jxl_array_len(&bins)) {
        if (idx + 1 < jxl_array_len(&bins)) {
          memmove(jxl_array_data(&bins) + idx, jxl_array_data(&bins) + idx + 1,
                  (jxl_array_len(&bins) - idx - 1) * sizeof(jxl_entropy_delta));
        }
        --bins.len;
      }
    }
    // From now on `rest` is the height of balancing bin,
    // here it can be negative, but will be tracted into positive domain later
    rest += *jxl_array_at(&self->counts_, remainder_pos);

    if (!jxl_array_empty(&bins)) {
      const uint32_t max_log = ac[1].step_log;
      while (true) {
        // Update balancing bin penalties setting guards and tractors
        for (uint32_t log = 0; log <= max_log; ++log) {
          jxl_ans_hist_bin delta = 1 << log;
          if (rest >= table_size) {
            // Tract large `rest` into allowed domain:
            balance_inc[log] = 0;  // permit all inc steps
            balance_dec[log] = 0;  // forbid all dec steps
          } else if (rest > 1) {
            // `rest` is OK, put guards against non-possible steps
            balance_inc[log] =
                rest > delta  // possible step
                    ? max_freq * (int64_t)(*jxl_ans_encoding_histogram_lg2_lut_at_const(jxl_ans_encoding_histogram_lg2(), rest) - *jxl_ans_encoding_histogram_lg2_lut_at_const(jxl_ans_encoding_histogram_lg2(), rest - delta))
                    : INT64_MAX;  // forbidden
            balance_dec[log] =
                rest + delta < table_size  // possible step
                    ? max_freq * (int64_t)(*jxl_ans_encoding_histogram_lg2_lut_at_const(jxl_ans_encoding_histogram_lg2(), rest + delta) - *jxl_ans_encoding_histogram_lg2_lut_at_const(jxl_ans_encoding_histogram_lg2(), rest))
                    : 0;  // forbidden
          } else {
            // Tract negative or zero `rest` into positive:
            // forbid all inc steps
            balance_inc[log] = INT64_MAX;
            // permit all dec steps
            balance_dec[log] = INT64_MAX;
          }
        }
        // Try to increase entropy
        // Truncation is OK here, accuracy is anyway better than float.
        jxl_entropy_delta* best_bin_inc = jxl_array_data(&bins);
        for (jxl_entropy_delta* p = jxl_array_data(&bins) + 1; p != jxl_array_data(&bins) + jxl_array_len(&bins);
             ++p) {
          int64_t best_inc =
              best_bin_inc->freq *
                  (int64_t)(ac[best_bin_inc->count_ind].delta_lg2) -
              balance_inc[ac[best_bin_inc->count_ind].step_log];
          int64_t cand_inc =
              p->freq * (int64_t)(ac[p->count_ind].delta_lg2) -
              balance_inc[ac[p->count_ind].step_log];
          if ((best_inc >> ac[best_bin_inc->count_ind].step_log) <
              (cand_inc >> ac[p->count_ind].step_log)) {
            best_bin_inc = p;
          }
        }
        int64_t best_inc =
            best_bin_inc->freq *
                (int64_t)(ac[best_bin_inc->count_ind].delta_lg2) -
            balance_inc[ac[best_bin_inc->count_ind].step_log];
        if (best_inc > 0) {
          // Grow the bin with the best histogram entropy increase
          rest -= 1 << ac[best_bin_inc->count_ind--].step_log;
        } else {
          // This still implies that entropy is strictly increasing each step
          // (or `rest` is tracted into positive domain), so we cannot loop
          // infinitely
          jxl_entropy_delta* best_bin_dec = jxl_array_data(&bins);
          for (jxl_entropy_delta* p = jxl_array_data(&bins) + 1;
               p != jxl_array_data(&bins) + jxl_array_len(&bins); ++p) {
            int64_t best_dec =
                best_bin_dec->freq *
                    (int64_t)(ac[best_bin_dec->count_ind + 1].delta_lg2) -
                balance_dec[ac[best_bin_dec->count_ind + 1].step_log];
            int64_t cand_dec =
                p->freq * (int64_t)(ac[p->count_ind + 1].delta_lg2) -
                balance_dec[ac[p->count_ind + 1].step_log];
            if ((cand_dec >> ac[p->count_ind + 1].step_log) <
                (best_dec >> ac[best_bin_dec->count_ind + 1].step_log)) {
              best_bin_dec = p;
            }
          }
          int64_t best_dec =
              best_bin_dec->freq *
                  (int64_t)(ac[best_bin_dec->count_ind + 1].delta_lg2) -
              balance_dec[ac[best_bin_dec->count_ind + 1].step_log];
          // Break if no reverse steps can grow entropy (or valid)
          if (best_dec >= 0) break;
          // Decrease the bin with the best histogram entropy increase
          rest += 1 << ac[++best_bin_dec->count_ind].step_log;
        }
      }
      // Set counts besides the balancing bin
      for (size_t bi = 0; bi < jxl_array_len(&bins); ++bi) {
        *jxl_array_at(&self->counts_, jxl_array_at(&bins, bi)->bin_ind) = ac[jxl_array_at(&bins, bi)->count_ind].count;
      }

      // The scheme works fine if we have room to grow `bit_width` of balancing
      // bin, otherwise we need to put balancing bin to the first bin of 12 bit
      // width. In this case both that bin and balancing one should be close to
      // 2048 in targets, so exchange of them will not produce much worse
      // histogram
      for (size_t n = 0; n < remainder_pos; ++n) {
        if (*jxl_array_at(&self->counts_, n) >= 2048) {
          *jxl_array_at(&self->counts_, remainder_pos) = *jxl_array_at(&self->counts_, n);
          remainder_pos = n;
          break;
        }
      }
    }
    // Set balancing bin
    *jxl_array_at(&self->counts_, remainder_pos) = rest;
    self->omit_pos_ = remainder_pos;

    jxl_array_destroy(&bins);
    return *jxl_array_at(&self->counts_, remainder_pos) > 0;
  }

static jxl_status jxl_ans_encoding_histogram_compute_best(
    const jxl_histogram* histo, const jxl_array_i32* counts,
    jxl_ans_histogram_strategy ans_histogram_strategy,
    jxl_ans_encoding_histogram* out){
    jxl_ans_encoding_histogram result;
    jxl_ans_encoding_histogram_construct_empty(&result, counts->memory_manager);

    result.alphabet_size_ = jxl_histogram_alphabet_size(counts);
    if (result.alphabet_size_ > ANS_MAX_ALPHABET_SIZE)
      return JXL_FAILURE("Too many entries in an ANS histogram");

    if (result.alphabet_size_ > 0) {
      // Flat code
      result.method_ = 0;
      result.num_symbols_ = result.alphabet_size_;
      JXL_RETURN_IF_ERROR(
          jxl_create_flat_histogram(&result.counts_, result.alphabet_size_,
                              ANS_TAB_SIZE, counts->memory_manager));
      // in this case length can be non-suitable for SIMD - fix it
      JXL_RETURN_IF_ERROR(jxl_array_resize_zero(&result.counts_, jxl_array_len(counts)));
      jxl_size_writer writer;
      jxl_size_writer_construct_empty(&writer);
      jxl_bit_sink sink = jxl_make_size_writer_bit_sink(&writer);
      JXL_RETURN_IF_ERROR(jxl_ans_encoding_histogram_encode(&result, &sink));
      result.cost_ = writer.size + jxl_ans_encoding_histogram_estimate_data_bits_flat(histo, counts);
    } else {
      // Empty histogram
      result.method_ = 1;
      result.num_symbols_ = 0;
      result.cost_ = 3;
      jxl_ans_encoding_histogram_swap(out, &result);
      return jxl_ok_status();
    }

    size_t symbol_count = 0;
    for (size_t n = 0; n < result.alphabet_size_; ++n) {
      if (*jxl_array_at_const(counts, n) > 0) {
        if (symbol_count < kMaxNumSymbolsForSmallCode) {
          result.symbols_[symbol_count] = n;
        }
        ++symbol_count;
      }
    }
    result.num_symbols_ = symbol_count;
    if (symbol_count == 1) {
      // Single-bin histogram
      result.method_ = 1;
      if (!jxl_status_ok(jxl_array_copy_from(&result.counts_, counts))) JXL_CRASH();
      *jxl_array_at(&result.counts_, result.symbols_[0]) = ANS_TAB_SIZE;
      jxl_size_writer writer;
      jxl_size_writer_construct_empty(&writer);
      jxl_bit_sink sink = jxl_make_size_writer_bit_sink(&writer);
      JXL_RETURN_IF_ERROR(jxl_ans_encoding_histogram_encode(&result, &sink));
      result.cost_ = writer.size;
      jxl_ans_encoding_histogram_swap(out, &result);
      return jxl_ok_status();
    }

    // Here min 2 symbols
    jxl_ans_encoding_histogram normalized;
    jxl_ans_encoding_histogram_construct_empty(&normalized, counts->memory_manager);
    if (!jxl_status_ok(jxl_ans_encoding_histogram_copy_from(&normalized, &result))) JXL_CRASH();
    switch (ans_histogram_strategy) {
      case kANSHistPrecise:
        for (uint32_t shift = 0; shift < ANS_LOG_TAB_SIZE; shift++) {
          JXL_RETURN_IF_ERROR(
              jxl_ans_encoding_histogram_try_ans_histogram_shift(shift, &normalized, &result, histo, counts));
        }
        break;
      case kANSHistApproximate:
        for (uint32_t shift = 0; shift <= ANS_LOG_TAB_SIZE; shift += 2) {
          JXL_RETURN_IF_ERROR(
              jxl_ans_encoding_histogram_try_ans_histogram_shift(shift, &normalized, &result, histo, counts));
        }
        break;
      case kANSHistFast:
        JXL_RETURN_IF_ERROR(
            jxl_ans_encoding_histogram_try_ans_histogram_shift(0, &normalized, &result, histo, counts));
        JXL_RETURN_IF_ERROR(jxl_ans_encoding_histogram_try_ans_histogram_shift(
            ANS_LOG_TAB_SIZE / 2, &normalized, &result, histo, counts));
        JXL_RETURN_IF_ERROR(jxl_ans_encoding_histogram_try_ans_histogram_shift(
            ANS_LOG_TAB_SIZE, &normalized, &result, histo, counts));
        break;
    }

      // Sanity check
#if JXL_IS_DEBUG_BUILD
    JXL_ENSURE(jxl_array_len(counts) == jxl_array_len(&result.counts_));
    jxl_ans_hist_bin total = 0;  // Used only in assert.
    for (size_t i = 0; i < result.alphabet_size_; ++i) {
      JXL_ENSURE(*jxl_array_at(&result.counts_, i) >= 0);
      // For non-flat histogram values should be zero or non-zero simultaneously
      // for the same symbol in both initial and normalized histograms.
      JXL_ENSURE(result.method_ == 0 ||
                 (*jxl_array_at_const(counts, i) > 0) == (*jxl_array_at(&result.counts_, i) > 0));
      // Check accuracy of the histogram values
      if (result.method_ > 0 && *jxl_array_at(&result.counts_, i) > 0 &&
          i != result.omit_pos_) {
        int logcounts = jxl_floor_log2_nonzero32(
            (uint32_t)(*jxl_array_at(&result.counts_, i)));
        int bitcount =
            jxl_get_population_count_precision(logcounts, result.method_ - 1);
        int drop_bits = logcounts - bitcount;
        // Check that the value is divisible by 2^drop_bits
        JXL_ENSURE((*jxl_array_at(&result.counts_, i) & ((1 << drop_bits) - 1)) == 0);
      }
      total += *jxl_array_at(&result.counts_, i);
    }
    for (size_t i = result.alphabet_size_; i < jxl_array_len(&result.counts_); ++i) {
      JXL_ENSURE(*jxl_array_at_const(counts, i) == 0);
      JXL_ENSURE(*jxl_array_at(&result.counts_, i) == 0);
    }
    JXL_ENSURE((histo->total_count == 0) || (total == ANS_TAB_SIZE));
#endif
    jxl_array_destroy(&normalized.counts_);
    jxl_ans_encoding_histogram_swap(out, &result);
    return jxl_ok_status();
}

jxl_status jxl_histogram_ans_population_cost_impl(const jxl_histogram* h, const jxl_array_i32* counts,
                                  float* out){
  if (jxl_array_len(counts) > ANS_MAX_ALPHABET_SIZE) {
    *out = FLT_MAX;
    return jxl_ok_status();
  }
  jxl_ans_encoding_histogram normalized;
  jxl_ans_encoding_histogram_construct_empty(&normalized, counts->memory_manager);
  JXL_RETURN_IF_ERROR(jxl_ans_encoding_histogram_compute_best(
      h, counts, kANSHistFast,
      &normalized));
  *out = jxl_ans_encoding_histogram_cost(&normalized);
  jxl_array_destroy(&normalized.counts_);
  return jxl_ok_status();
}

typedef struct jxl_huff_ctx {
  jxl_array_u32* histo;
  size_t size;
  jxl_array_u8* depths;
  jxl_array_u16* bits;
  jxl_bit_writer* writer;
} jxl_huff_ctx;

static jxl_status jxl_build_and_store_huffman_tree_body(void* opaque) {
  jxl_huff_ctx* c = (jxl_huff_ctx*)(opaque);
  return jxl_build_and_store_huffman_tree(jxl_array_data(c->histo), c->size, jxl_array_data(c->depths),
                                  jxl_array_data(c->bits), c->writer);
}

// Returns an estimate or exact cost of encoding this histogram and the
// corresponding data.
jxl_status jxl_entropy_encoding_data_build_and_store_ans_encoding_data(
    jxl_entropy_encoding_data* self, jxl_memory_manager* memory_manager,
    jxl_ans_histogram_strategy ans_histogram_strategy,
    const jxl_histogram* histogram, const jxl_array_i32* counts,
    jxl_bit_writer* writer, size_t* cost_out){
  JXL_ENSURE(!jxl_array_empty(&self->encoding_info_starts));
  jxl_ans_enc_symbol_info* info =
      jxl_array_data(&self->encoding_info) + jxl_array_back(&self->encoding_info_starts);
  size_t size = jxl_histogram_alphabet_size(counts);
  if (self->use_prefix_code) {
    size_t cost = 0;
    if (size <= 1) {
      *cost_out = 0;
      return jxl_ok_status();
    }
    jxl_array_u32 histo;
    jxl_array_construct_empty(&histo, memory_manager);
    jxl_array_u8 depths;
    jxl_array_construct_empty(&depths, memory_manager);
    jxl_array_u16 bits;
    jxl_array_construct_empty(&bits, memory_manager);
    jxl_status status = jxl_array_resize_zero(&histo, size);
    if (!jxl_status_ok(status)) {
      jxl_array_destroy(&histo);
      jxl_array_destroy(&depths);
      jxl_array_destroy(&bits);
      return status;
    }
    for (size_t i = 0; i < size; i++) {
      JXL_ENSURE(*jxl_array_at_const(counts, i) >= 0);
      *jxl_array_at(&histo, i) = *jxl_array_at_const(counts, i);
    }
    status = jxl_array_resize_zero(&depths, size);
    if (!jxl_status_ok(status)) {
      jxl_array_destroy(&histo);
      jxl_array_destroy(&depths);
      jxl_array_destroy(&bits);
      return status;
    }
    status = jxl_array_resize_zero(&bits, size);
    if (!jxl_status_ok(status)) {
      jxl_array_destroy(&histo);
      jxl_array_destroy(&depths);
      jxl_array_destroy(&bits);
      return status;
    }
    if (writer == NULL) {
      jxl_bit_writer tmp_writer;
      jxl_bit_writer_make(memory_manager, &tmp_writer);
      jxl_huff_ctx huff_ctx = {&histo, size, &depths, &bits, &tmp_writer};
      status = jxl_bit_writer_with_max_bits(&tmp_writer,
          8 * size + 8,  // safe upper bound
          kLayerHeader, jxl_build_and_store_huffman_tree_body, &huff_ctx);
      cost = jxl_bit_writer_bits_written(&tmp_writer);
      jxl_bit_writer_destroy(&tmp_writer);
      if (!jxl_status_ok(status)) {
        jxl_array_destroy(&histo);
        jxl_array_destroy(&depths);
        jxl_array_destroy(&bits);
        return status;
      }
    } else {
      size_t start = jxl_bit_writer_bits_written(writer);
      status = jxl_build_and_store_huffman_tree(
          jxl_array_data(&histo), size, jxl_array_data(&depths), jxl_array_data(&bits), writer);
      if (!jxl_status_ok(status)) {
        jxl_array_destroy(&histo);
        jxl_array_destroy(&depths);
        jxl_array_destroy(&bits);
        return status;
      }
      cost = jxl_bit_writer_bits_written(writer) - start;
    }
    for (size_t i = 0; i < size; i++) {
      info[i].bits = *jxl_array_at(&depths, i) == 0 ? 0 : *jxl_array_at(&bits, i);
      info[i].depth = *jxl_array_at(&depths, i);
    }
    // Estimate data cost.
    for (size_t i = 0; i < size; i++) {
      cost += *jxl_array_at(&histo, i) * info[i].depth;
    }
    *cost_out = cost;
    jxl_array_destroy(&histo);
    jxl_array_destroy(&depths);
    jxl_array_destroy(&bits);
    return jxl_ok_status();
  }
  jxl_ans_encoding_histogram normalized;
  jxl_ans_encoding_histogram_construct_empty(&normalized, memory_manager);
  JXL_RETURN_IF_ERROR(
      jxl_ans_encoding_histogram_compute_best(histogram, counts,
                                        ans_histogram_strategy, &normalized));

  // TODO(eustas): fix: 2KiB on stack
  jxl_alias_table_entry a[ANS_MAX_ALPHABET_SIZE];

  JXL_RETURN_IF_ERROR(
      jxl_init_alias_table(memory_manager,
                     jxl_array_data_const(jxl_ans_encoding_histogram_counts(&normalized)),
                     jxl_array_len(jxl_ans_encoding_histogram_counts(&normalized)),
                     ANS_LOG_TAB_SIZE, self->log_alpha_size, a));
  jxl_ans_encoding_histogram_ans_build_info_table(&normalized, a, self->log_alpha_size, info, &self->ans_reverse_maps);
  if (writer != NULL) {
    // size_t start = jxl_bit_writer_bits_written(writer);
    jxl_bit_sink sink = jxl_make_bit_writer_bit_sink(writer);
    JXL_RETURN_IF_ERROR(jxl_ans_encoding_histogram_encode(&normalized, &sink));
    // return jxl_bit_writer_bits_written(writer) - start;
  }
  *cost_out = (size_t)(ceilf(jxl_ans_encoding_histogram_cost(&normalized)));
  jxl_array_destroy(&normalized.counts_);
  return jxl_ok_status();
}

typedef struct jxl_write_tok_ctx {
  const jxl_token_stream* tokens;
  const jxl_entropy_encoding_data* codes;
  size_t context_offset;
  jxl_bit_writer* writer;
} jxl_write_tok_ctx;

static jxl_status jxl_write_tokens_body(void* opaque) {
  jxl_write_tok_ctx* c = (jxl_write_tok_ctx*)(opaque);
  jxl_write_tokens_with_allotment(c->tokens, c->codes, c->context_offset, c->writer);
  return jxl_ok_status();
}

static void jxl_histogram_from_symbol_info(const jxl_ans_enc_symbol_info* encoding_info,
                             size_t alphabet_size, bool use_prefix_code,
                             jxl_histogram* histo, jxl_array_i32* counts) {
  if (!jxl_status_ok(jxl_array_resize_zero(counts, jxl_div_ceil(alphabet_size, kHistogramRounding) *
                                kHistogramRounding))) {
    // Caller ignores jxl_status; OOM is fatal here.
    JXL_CRASH();
  }
  histo->total_count = 0;
  histo->entropy = 0;
  for (size_t i = 0; i < alphabet_size; ++i) {
    const jxl_ans_enc_symbol_info* info = &encoding_info[i];
    int count = use_prefix_code
                    ? (info->depth ? (1u << (PREFIX_MAX_BITS - info->depth)) : 0)
                    : info->freq_;
    *jxl_array_at(counts, i) = count;
    histo->total_count += count;
  }
}

jxl_status jxl_entropy_encoding_data_choose_uint_configs_scratch(
    jxl_entropy_encoding_data* self, jxl_memory_manager* memory_manager,
    const jxl_histogram_params* params, const jxl_token_streams* tokens,
    jxl_array_histogram* clustered_histograms, jxl_hist_count_streams* clustered_counts,
    jxl_array_hybrid_uint_config* configs, jxl_array_u8* is_valid,
    jxl_array_size* histo_volume, jxl_array_size* histo_offset,
    jxl_array_u32* max_value_per_histo, jxl_array_u32* transposed,
    jxl_aligned_memory* tmp) {
  // Brute-force method that tries a few options.
  if (params->uint_method == kHybridUintBest) {
    JXL_RETURN_IF_ERROR(jxl_array_hybrid_uint_config_push_back(configs, jxl_hybrid_uint_config_make(4, 2, 0)));
    JXL_RETURN_IF_ERROR(jxl_array_hybrid_uint_config_push_back(configs, jxl_hybrid_uint_config_make(5, 2, 0)));
    JXL_RETURN_IF_ERROR(jxl_array_hybrid_uint_config_push_back(configs, jxl_hybrid_uint_config_make(0, 0, 0)));
  } else {
    JXL_DASSERT(params->uint_method == kHybridUintFast);
    JXL_RETURN_IF_ERROR(jxl_array_hybrid_uint_config_push_back(configs, jxl_hybrid_uint_config_make(4, 2, 0)));
    JXL_RETURN_IF_ERROR(jxl_array_hybrid_uint_config_push_back(configs, jxl_hybrid_uint_config_make(0, 0, 0)));
  }

  size_t num_histo = jxl_array_len(clustered_histograms);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(is_valid, num_histo));
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(histo_volume, 2 * num_histo));
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(histo_offset, 2 * num_histo + 1));
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(max_value_per_histo, 2 * num_histo));

  // TODO(veluca): do not ignore self->lz77 commands.

  for (size_t stream_i = 0; stream_i < jxl_token_streams_size(tokens); ++stream_i) {
    const jxl_token_stream* stream = jxl_token_streams_at_const(tokens, stream_i);
    for (size_t token_i = 0; token_i < jxl_array_len(stream); ++token_i) {
      const jxl_token* token = jxl_array_at_const(stream, token_i);
      size_t histo = *jxl_array_at(&self->context_map, token->context);
      (*jxl_array_at(histo_volume, histo + (token->is_lz77_length ? num_histo : 0)))++;
    }
  }
  size_t max_histo_volume = 0;
  for (size_t h = 0; h < 2 * num_histo; ++h) {
    max_histo_volume = JXL_MAX(max_histo_volume, *jxl_array_at(histo_volume, h));
    *jxl_array_at(histo_offset, h + 1) = *jxl_array_at(histo_offset, h) + *jxl_array_at(histo_volume, h);
  }

  const size_t max_vec_size = jxl_max_vector_size();
  JXL_RETURN_IF_ERROR(
      jxl_array_resize_zero(transposed, *jxl_array_at(histo_offset, num_histo * 2) + max_vec_size));
  {
    jxl_array_size next_offset;
    jxl_array_construct_empty(&next_offset, memory_manager);
    if (!jxl_status_ok(jxl_array_copy_from(&next_offset, histo_offset))) JXL_CRASH();
    for (size_t stream_i = 0; stream_i < jxl_token_streams_size(tokens); ++stream_i) {
      const jxl_token_stream* stream = jxl_token_streams_at_const(tokens, stream_i);
      for (size_t token_i = 0; token_i < jxl_array_len(stream); ++token_i) {
        const jxl_token* token = jxl_array_at_const(stream, token_i);
        size_t histo =
            *jxl_array_at(&self->context_map, token->context) + (token->is_lz77_length ? num_histo : 0);
        *jxl_array_at(transposed, (*jxl_array_at(&next_offset, histo))++) = token->value;
      }
    }
    jxl_array_destroy(&next_offset);
  }
  for (size_t h = 0; h < 2 * num_histo; ++h) {
    *jxl_array_at(max_value_per_histo, h) =
        jxl_max_value(jxl_array_data(transposed) + *jxl_array_at(histo_offset, h), *jxl_array_at(histo_volume, h));
  }
  uint32_t max_lz77 = 0;
  for (size_t h = num_histo; h < 2 * num_histo; ++h) {
    max_lz77 = JXL_MAX(max_lz77, jxl_max_value(jxl_array_data(transposed) + *jxl_array_at(histo_offset, h),
                                           *jxl_array_at(histo_volume, h)));
  }

  // Wider histograms are assigned max cost in PopulationCost anyway
  // and therefore will not be used
  size_t max_alpha = ANS_MAX_ALPHABET_SIZE;

  jxl_status status = jxl_aligned_memory_create(
      memory_manager, (max_histo_volume + max_vec_size) * sizeof(uint32_t), 0,
      tmp);
  if (!jxl_status_ok(status)) {
    return status;
  }
  for (size_t h = 0; h < num_histo; h++) {
    float best_cost = FLT_MAX;
    for (size_t cfg_i = 0; cfg_i < jxl_array_len(configs); ++cfg_i) {
      jxl_hybrid_uint_config cfg = *jxl_array_at(configs, cfg_i);
      uint32_t max_v = *jxl_array_at(max_value_per_histo, h);
      size_t capacity;
      {
        uint32_t tok, nbits, bits;
        jxl_hybrid_uint_config_encode(cfg, max_v, &tok, &nbits, &bits);
        tok |= jxl_hybrid_uint_config_lsb_mask(cfg);
        if (tok >= max_alpha || (self->lz77.enabled && tok >= self->lz77.min_symbol)) {
          continue;  // Not valid config for this context
        }
        capacity = tok + 1;
      }

      jxl_histogram histo;
      jxl_array_i32 histo_counts;
      jxl_array_construct_empty(&histo_counts, memory_manager);
      jxl_histogram_ensure_capacity(&histo_counts, capacity);
      size_t len = *jxl_array_at(histo_volume, h);
      uint32_t* data = jxl_array_data(transposed) + *jxl_array_at(histo_offset, h);
      size_t extra_bits = jxl_estimate_token_cost(data, len, cfg, tmp);
      uint32_t* tmp_tokens = (uint32_t*)(jxl_aligned_memory_address(tmp));
      for (size_t i = 0; i < len; ++i) {
        jxl_histogram_fast_add(&histo_counts, tmp_tokens[i]);
      }
      jxl_histogram_condition(&histo, &histo_counts);
      float cost;
      status = jxl_histogram_ans_population_cost_impl(&histo, &histo_counts, &cost);
      if (!jxl_status_ok(status)) {
        return status;
      }
      cost += extra_bits;
      // Add signaling cost of the hybriduintconfig itself.
      cost += jxl_ceil_log2_nonzero32(cfg.split_exponent + 1);
      cost += jxl_ceil_log2_nonzero32(cfg.split_exponent - cfg.msb_in_token + 1);
      if (cost < best_cost) {
        *jxl_array_at(&self->uint_config, h) = cfg;
        best_cost = cost;
        jxl_histogram_swap(jxl_array_at(clustered_histograms, h), jxl_hist_count_streams_at(clustered_counts, h), &histo,
                      &histo_counts);
      }
      jxl_array_destroy(&histo_counts);
    }
  }

  size_t max_tok = 0;
  for (size_t h = 0; h < num_histo; ++h) {
    jxl_histogram* histo = jxl_array_at(clustered_histograms, h);
    jxl_array_i32* histo_counts = jxl_hist_count_streams_at(clustered_counts, h);
    max_tok = JXL_MAX(max_tok, jxl_histogram_max_symbol(histo, histo_counts));
    size_t len = *jxl_array_at(histo_volume, num_histo + h);
    if (len == 0) continue;  // E.g. when lz77 not enabled
    size_t max_histo_tok = *jxl_array_at(max_value_per_histo, num_histo + h);
    uint32_t tok, nbits, bits;
    jxl_hybrid_uint_config_encode(self->lz77.length_uint_config, max_histo_tok, &tok, &nbits, &bits);
    tok |= jxl_hybrid_uint_config_lsb_mask(self->lz77.length_uint_config);
    tok += self->lz77.min_symbol;
    jxl_histogram_ensure_capacity(histo_counts, tok + 1);
    uint32_t* data = jxl_array_data(transposed) + *jxl_array_at(histo_offset, num_histo + h);
    uint32_t unused =
        jxl_estimate_token_cost(data, len, self->lz77.length_uint_config, tmp);
    (void)unused;
    uint32_t* tmp_tokens = (uint32_t*)(jxl_aligned_memory_address(tmp));
    for (size_t i = 0; i < len; ++i) {
      jxl_histogram_fast_add(histo_counts, tmp_tokens[i] + self->lz77.min_symbol);
    }
    jxl_histogram_condition(histo, histo_counts);
    max_tok = JXL_MAX(max_tok, jxl_histogram_max_symbol(histo, histo_counts));
  }

  // log_alpha_size - 5 is encoded in the header, so min is 5.
  size_t log_size = 5;
  while (max_tok >= (1u << log_size)) ++log_size;

  size_t max_log_alpha_size = self->use_prefix_code ? PREFIX_MAX_BITS : 8;
  JXL_ENSURE(log_size <= max_log_alpha_size);

  if (self->use_prefix_code) {
    self->log_alpha_size = PREFIX_MAX_BITS;
  } else {
    self->log_alpha_size = log_size;
  }

  return jxl_ok_status();
}

jxl_status jxl_entropy_encoding_data_choose_uint_configs(
    jxl_entropy_encoding_data* self, jxl_memory_manager* memory_manager,
    const jxl_histogram_params* params, const jxl_token_streams* tokens,
    jxl_array_histogram* clustered_histograms,
    jxl_hist_count_streams* clustered_counts){
  // Set sane default log_alpha_size.
  if (self->use_prefix_code) {
    self->log_alpha_size = PREFIX_MAX_BITS;
  } else if (self->lz77.enabled) {
    self->log_alpha_size = 8;
  } else {
    self->log_alpha_size = 7;
  }

jxl_array_clear(&self->uint_config);
  JXL_RETURN_IF_ERROR(jxl_array_hybrid_uint_config_resize_fill(&self->uint_config, jxl_array_len(clustered_histograms),
                                      jxl_histogram_params_uint_config(params)));
  // If the uint config is fixed, just use it.
  if (params->uint_method != kHybridUintBest &&
      params->uint_method != kHybridUintFast) {
    return jxl_ok_status();
  }

  jxl_array_hybrid_uint_config configs;
  jxl_array_construct_empty(&configs, memory_manager);
  jxl_array_u8 is_valid;
  jxl_array_construct_empty(&is_valid, memory_manager);
  jxl_array_size histo_volume;
  jxl_array_construct_empty(&histo_volume, memory_manager);
  jxl_array_size histo_offset;
  jxl_array_construct_empty(&histo_offset, memory_manager);
  jxl_array_u32 max_value_per_histo;
  jxl_array_construct_empty(&max_value_per_histo, memory_manager);
  jxl_array_u32 transposed;
  jxl_array_construct_empty(&transposed, memory_manager);
  jxl_aligned_memory tmp;
  jxl_aligned_memory_construct_empty(&tmp);
  jxl_status status = jxl_entropy_encoding_data_choose_uint_configs_scratch(
      self, memory_manager, params, tokens, clustered_histograms,
      clustered_counts, &configs, &is_valid, &histo_volume, &histo_offset,
      &max_value_per_histo, &transposed, &tmp);
  jxl_array_destroy(&configs);
  jxl_array_destroy(&is_valid);
  jxl_array_destroy(&histo_volume);
  jxl_array_destroy(&histo_offset);
  jxl_array_destroy(&max_value_per_histo);
  jxl_array_destroy(&transposed);
  jxl_aligned_memory_destroy(&tmp);
  return status;
}

typedef struct jxl_store_ans_sym_ctx {
  jxl_entropy_encoding_data* self;
  jxl_memory_manager* memory_manager;
  jxl_ans_histogram_strategy ans_histogram_strategy;
  jxl_histogram* clustered_histogram;
  jxl_array_i32* clustered_count;
  jxl_bit_writer* writer;
  size_t* cost;
} jxl_store_ans_sym_ctx;

jxl_status jxl_store_ans_symbol_encoding_body(void* opaque) {
  jxl_store_ans_sym_ctx* x = (jxl_store_ans_sym_ctx*)(opaque);
  size_t ans_cost;
  JXL_RETURN_IF_ERROR(jxl_entropy_encoding_data_build_and_store_ans_encoding_data(x->self, 
      x->memory_manager, x->ans_histogram_strategy, x->clustered_histogram,
      x->clustered_count, x->writer, &ans_cost));
  *x->cost += ans_cost;
  return jxl_ok_status();
}

// Returns cost (in bits).
jxl_status jxl_entropy_encoding_data_build_and_store_entropy_codes_body(
    jxl_entropy_encoding_data* self, jxl_memory_manager* memory_manager,
    const jxl_histogram_params* params, const jxl_token_streams* tokens,
    const jxl_array_histogram* builder, const jxl_hist_count_streams* builder_counts,
    jxl_bit_writer* writer, jxl_layer_type layer, size_t* cost_out,
    jxl_array_histogram* clustered_histograms, jxl_hist_count_streams* clustered_counts) {
  const size_t prev_histograms = jxl_entropy_encoding_data_num_histograms(self);
  if (!jxl_status_ok(jxl_array_reserve(clustered_histograms, prev_histograms + jxl_array_len(builder)))) {
    JXL_CRASH();
  }
  JXL_RETURN_IF_ERROR(
      jxl_hist_count_streams_reserve(clustered_counts,
                              prev_histograms + jxl_array_len(builder)));
  for (size_t i = 0; i < prev_histograms; ++i) {
    jxl_histogram histo;
    jxl_array_i32 counts;
    jxl_array_construct_empty(&counts, memory_manager);
    jxl_histogram_from_symbol_info(jxl_array_data(&self->encoding_info) + *jxl_array_at(&self->encoding_info_starts, i),
                            jxl_entropy_encoding_data_alphabet_size(self, i), self->use_prefix_code, &histo, &counts);
    if (!jxl_status_ok(jxl_array_histogram_push_back(clustered_histograms, histo))) JXL_CRASH();
    JXL_RETURN_IF_ERROR(jxl_hist_count_streams_emplace_back(clustered_counts, &counts));
    jxl_array_destroy(&counts);
  }
  size_t context_offset = jxl_array_len(&self->context_map);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(&self->context_map, context_offset + jxl_array_len(builder)));
  if (jxl_array_len(builder) > 1) {
    jxl_array_u32 histogram_symbols;
    jxl_array_construct_empty(&histogram_symbols, memory_manager);
    jxl_status cluster_status = jxl_cluster_histograms(
        params, builder, builder_counts, kClustersLimit, clustered_histograms,
        clustered_counts, &histogram_symbols);
    if (!jxl_status_ok(cluster_status)) {
      jxl_array_destroy(&histogram_symbols);
      return cluster_status;
    }
    for (size_t c = 0; c < jxl_array_len(builder); ++c) {
      *jxl_array_at(&self->context_map, context_offset + c) =
          (uint8_t)(*jxl_array_at(&histogram_symbols, c));
    }
    jxl_array_destroy(&histogram_symbols);
    if (writer != NULL) {
      JXL_RETURN_IF_ERROR(jxl_encode_context_map(
          &self->context_map, jxl_array_len(clustered_histograms), writer, layer));
    }
  } else {
    JXL_ENSURE(jxl_array_empty(&self->encoding_info_starts));
    if (!jxl_status_ok(jxl_array_histogram_push_back(clustered_histograms, *jxl_array_at_const(builder, 0)))) JXL_CRASH();
    JXL_RETURN_IF_ERROR(jxl_hist_count_streams_push_back(
        clustered_counts, jxl_hist_count_streams_at_const(builder_counts, 0)));
  }

  JXL_RETURN_IF_ERROR(jxl_entropy_encoding_data_choose_uint_configs(self, memory_manager, params, tokens,
                                        clustered_histograms,
                                        clustered_counts));

  jxl_size_writer size_writer;  // Used if writer == NULL to estimate costs.
  jxl_size_writer_construct_empty(&size_writer);
  size_t cost = self->use_prefix_code ? 1 : 3;

  if (writer) jxl_bit_writer_write(writer, 1, TO_JXL_BOOL(self->use_prefix_code));
  if (writer == NULL) {
    jxl_bit_sink sw_sink = jxl_make_size_writer_bit_sink(&size_writer);
    jxl_encode_uint_configs(&self->uint_config, &sw_sink, self->log_alpha_size);
  } else {
    if (!self->use_prefix_code) jxl_bit_writer_write(writer, 2, self->log_alpha_size - 5);
    jxl_bit_sink bw_sink = jxl_make_bit_writer_bit_sink(writer);
    jxl_encode_uint_configs(&self->uint_config, &bw_sink, self->log_alpha_size);
  }
  if (self->use_prefix_code) {
    for (size_t i = 0; i < jxl_array_len(clustered_histograms); ++i) {
      size_t alphabet_size =
          JXL_MAX((size_t)(1), jxl_histogram_alphabet_size(jxl_hist_count_streams_at(clustered_counts, i)));
      if (writer) {
        jxl_bit_sink bw_sink = jxl_make_bit_writer_bit_sink(writer);
        jxl_store_var_len_uint16(alphabet_size - 1, &bw_sink);
      } else {
        jxl_bit_sink sw_sink = jxl_make_size_writer_bit_sink(&size_writer);
        jxl_store_var_len_uint16(alphabet_size - 1, &sw_sink);
      }
    }
  }
  cost += size_writer.size;
  for (size_t c = prev_histograms; c < jxl_array_len(clustered_histograms); ++c) {
    size_t alphabet_size = jxl_histogram_alphabet_size(jxl_hist_count_streams_at(clustered_counts, c));
    if (!jxl_status_ok(jxl_array_u32_push_back(&self->encoding_info_starts,
                       (uint32_t)(jxl_array_len(&self->encoding_info)))) ||
        !jxl_status_ok(jxl_array_resize_zero(&self->encoding_info, jxl_array_len(&self->encoding_info) + alphabet_size))) {
      JXL_CRASH();
    }
    jxl_store_ans_sym_ctx ans_sym_ctx = {
        self, memory_manager, params->ans_histogram_strategy,
        jxl_array_at(clustered_histograms, c), jxl_hist_count_streams_at(clustered_counts, c), writer, &cost};
    if (writer) {
      JXL_RETURN_IF_ERROR(jxl_bit_writer_with_max_bits(writer, 
          256 + alphabet_size * 24, layer, jxl_store_ans_symbol_encoding_body,
          &ans_sym_ctx));
    } else {
      JXL_RETURN_IF_ERROR(jxl_store_ans_symbol_encoding_body(&ans_sym_ctx));
    }
  }
  *cost_out = cost;
  return jxl_ok_status();
}

jxl_status jxl_entropy_encoding_data_build_and_store_entropy_codes(
    jxl_entropy_encoding_data* self, jxl_memory_manager* memory_manager,
    const jxl_histogram_params* params, const jxl_token_streams* tokens,
    const jxl_array_histogram* builder, const jxl_hist_count_streams* builder_counts,
    jxl_bit_writer* writer, jxl_layer_type layer, size_t* cost_out){
  jxl_array_histogram clustered_histograms;
  jxl_array_construct_empty(&clustered_histograms, memory_manager);
  jxl_hist_count_streams clustered_counts;
  jxl_hist_count_streams_construct_empty(&clustered_counts);
  clustered_counts.memory_manager = memory_manager;
  jxl_status status = jxl_entropy_encoding_data_build_and_store_entropy_codes_body(
      self, memory_manager, params, tokens, builder, builder_counts, writer,
      layer, cost_out, &clustered_histograms, &clustered_counts);
  jxl_array_destroy(&clustered_histograms);
  jxl_hist_count_streams_destroy(&clustered_counts);
  return status;
}

void jxl_encode_uint_config(const jxl_hybrid_uint_config uint_config, jxl_bit_sink* sink,
                      size_t log_alpha_size) {
  jxl_bit_sink_write(sink, jxl_ceil_log2_nonzero32((uint32_t)(log_alpha_size + 1)),
               uint_config.split_exponent);
  if (uint_config.split_exponent == log_alpha_size) {
    return;  // msb/lsb don't matter.
  }
  size_t nbits = jxl_ceil_log2_nonzero32(uint_config.split_exponent + 1);
  jxl_bit_sink_write(sink, nbits, uint_config.msb_in_token);
  nbits = jxl_ceil_log2_nonzero32(uint_config.split_exponent -
                          uint_config.msb_in_token + 1);
  jxl_bit_sink_write(sink, nbits, uint_config.lsb_in_token);
}

void jxl_encode_uint_configs(const jxl_array_hybrid_uint_config* uint_config,
                       jxl_bit_sink* sink, size_t log_alpha_size){
  // TODO(veluca): RLE?
  for (size_t i = 0; i < jxl_array_len(uint_config); ++i) {
    jxl_encode_uint_config(*jxl_array_at_const(uint_config, i), sink, log_alpha_size);
  }
}

jxl_status jxl_build_and_encode_histograms_body(
    jxl_memory_manager* memory_manager, const jxl_histogram_params* params,
    size_t* num_contexts, jxl_token_streams* tokens, jxl_token_streams* tokens_lz77,
    jxl_entropy_encoding_data* codes, jxl_bit_writer* writer, jxl_layer_type layer,
    size_t* cost) {
  if (writer) {
    JXL_RETURN_IF_ERROR(jxl_bundle_write(&codes->lz77.fields, writer, layer));
  } else {
    size_t ebits, bits;
    JXL_RETURN_IF_ERROR(jxl_bundle_can_encode(&codes->lz77.fields, &ebits, &bits));
    (*cost) += bits;
  }
  if (codes->lz77.enabled) {
    if (writer) {
      size_t b = jxl_bit_writer_bits_written(writer);
      jxl_bit_sink lz_sink = jxl_make_bit_writer_bit_sink(writer);
      jxl_encode_uint_config(codes->lz77.length_uint_config, &lz_sink,
                       /*log_alpha_size=*/8);
      (*cost) += jxl_bit_writer_bits_written(writer) - b;
    } else {
      jxl_size_writer size_writer;
      jxl_size_writer_construct_empty(&size_writer);
      jxl_bit_sink lz_sink = jxl_make_size_writer_bit_sink(&size_writer);
      jxl_encode_uint_config(codes->lz77.length_uint_config, &lz_sink,
                       /*log_alpha_size=*/8);
      (*cost) += size_writer.size;
    }
    *num_contexts += 1;
    jxl_token_streams_swap(tokens, tokens_lz77);
  }
  size_t total_tokens = 0;
  // Build histograms.
  jxl_array_histogram builder;
  jxl_array_construct_empty(&builder, memory_manager);
  jxl_histogram hist_empty = jxl_histogram_empty();
  if (!jxl_status_ok(jxl_array_histogram_resize_fill(&builder, *num_contexts, hist_empty))) JXL_CRASH();
  jxl_hist_count_streams builder_counts;
  jxl_hist_count_streams_create(&builder_counts, *num_contexts, memory_manager);
  jxl_hybrid_uint_config uint_config = jxl_histogram_params_uint_config(params);
  for (size_t si = 0; si < jxl_token_streams_size(tokens); ++si) {
    const jxl_token_stream* stream = jxl_token_streams_at(tokens, si);
    if (codes->lz77.enabled) {
      for (size_t ti = 0; ti < jxl_array_len(stream); ++ti) {
        const jxl_token* token = jxl_array_at_const(stream, ti);
        total_tokens++;
        uint32_t tok, nbits, bits;
        jxl_hybrid_uint_config_encode(
            (token->is_lz77_length ? codes->lz77.length_uint_config
                                   : uint_config),
            token->value, &tok, &nbits, &bits);
        tok += token->is_lz77_length ? codes->lz77.min_symbol : 0;
        JXL_DASSERT(token->context < *num_contexts);
        jxl_histogram_add(jxl_array_at(&builder, token->context),
                     jxl_hist_count_streams_at(&builder_counts, token->context), tok);
      }
    } else if (*num_contexts == 1) {
      for (size_t ti = 0; ti < jxl_array_len(stream); ++ti) {
        const jxl_token* token = jxl_array_at_const(stream, ti);
        total_tokens++;
        uint32_t tok, nbits, bits;
        jxl_hybrid_uint_config_encode(uint_config, token->value, &tok, &nbits, &bits);
        jxl_histogram_add(jxl_array_at(&builder, 0), jxl_hist_count_streams_at(&builder_counts, 0), tok);
      }
    } else {
      for (size_t ti = 0; ti < jxl_array_len(stream); ++ti) {
        const jxl_token* token = jxl_array_at_const(stream, ti);
        total_tokens++;
        uint32_t tok, nbits, bits;
        jxl_hybrid_uint_config_encode(uint_config, token->value, &tok, &nbits, &bits);
        JXL_DASSERT(token->context < *num_contexts);
        jxl_histogram_add(jxl_array_at(&builder, token->context),
                     jxl_hist_count_streams_at(&builder_counts, token->context), tok);
      }
    }
  }

  bool use_prefix_code =
      params->force_huffman || total_tokens < 100 ||
      params->clustering == kClusteringFastest;
  if (!use_prefix_code) {
    bool all_singleton = true;
    for (size_t i = 0; i < *num_contexts; i++) {
      if (jxl_histogram_shannon_entropy(jxl_array_at(&builder, i), jxl_hist_count_streams_at(&builder_counts, i)) >= 1e-5) {
        all_singleton = false;
      }
    }
    if (all_singleton) {
      use_prefix_code = true;
    }
  }
  codes->use_prefix_code = use_prefix_code;

  // Encode histograms.
  size_t entropy_bits;
  jxl_status status = jxl_entropy_encoding_data_build_and_store_entropy_codes(codes, 
      memory_manager, params, tokens, &builder, &builder_counts, writer, layer,
      &entropy_bits);
  jxl_hist_count_streams_destroy(&builder_counts);
  jxl_array_destroy(&builder);
  JXL_RETURN_IF_ERROR(status);
  (*cost) += entropy_bits;
  return jxl_ok_status();
}

jxl_status jxl_build_and_encode_histograms(
    jxl_memory_manager* memory_manager, const jxl_histogram_params* params,
    size_t num_contexts, jxl_token_streams* tokens,
    jxl_entropy_encoding_data* codes, jxl_bit_writer* writer, jxl_layer_type layer,
    const jxl_array_size* image_widths, size_t* cost_out){
  codes->lz77.nonserialized_distance_context = num_contexts;
  codes->lz77.min_symbol = params->force_huffman ? 512 : 224;
  jxl_token_streams tokens_lz77;
  jxl_token_streams_construct_empty(&tokens_lz77);
  jxl_apply_lz77(params, num_contexts, tokens, &codes->lz77, image_widths,
            &tokens_lz77);
  if (!jxl_token_streams_empty(&tokens_lz77)) codes->lz77.enabled = true;

  size_t cost = 0;
  const size_t max_contexts = JXL_MIN(num_contexts, kClustersLimit);
  jxl_status status;
  if (writer) {
    jxl_bit_writer_allotment allotment;
    jxl_bit_writer_allotment_reset(&allotment,
                            128 + num_contexts * 40 + max_contexts * 96);
    jxl_status allotment_status = jxl_bit_writer_allotment_init(&allotment, writer);
    if (!jxl_status_ok(allotment_status)) {
      jxl_bit_writer_allotment_destroy(&allotment);
      jxl_token_streams_destroy(&tokens_lz77);
      return allotment_status;
    }
    jxl_status body_status = jxl_build_and_encode_histograms_body(
        memory_manager, params, &num_contexts, tokens, &tokens_lz77, codes,
        writer, layer, &cost);
    allotment_status = jxl_bit_writer_allotment_reclaim(&allotment, writer);
    jxl_bit_writer_allotment_destroy(&allotment);
    if (!jxl_status_ok(allotment_status)) {
      jxl_token_streams_destroy(&tokens_lz77);
      return allotment_status;
    }
    status = body_status;
  } else {
    status = jxl_build_and_encode_histograms_body(
        memory_manager, params, &num_contexts, tokens, &tokens_lz77, codes,
        writer, layer, &cost);
  }

  jxl_token_streams_destroy(&tokens_lz77);
  if (!jxl_status_ok(status)) return status;
  *cost_out = cost;
  return jxl_ok_status();
}

typedef struct jxl_ans_reversed_bit_buf {
  jxl_array_u64* out;
  jxl_array_u8* out_nbits;
  uint64_t allbits;
  size_t numallbits;
} jxl_ans_reversed_bit_buf;

void jxl_ans_reversed_bit_buf_add(jxl_ans_reversed_bit_buf* buf, size_t bits, size_t nbits) {
  if (JXL_UNLIKELY(nbits)) {
    JXL_DASSERT(bits >> nbits == 0);
    if (JXL_UNLIKELY(buf->numallbits + nbits > kBitWriterMaxBitsPerCall)) {
      if (!jxl_status_ok(jxl_array_u64_push_back(buf->out, buf->allbits))) JXL_CRASH();
      if (!jxl_status_ok(jxl_array_u8_push_back(buf->out_nbits, buf->numallbits))) JXL_CRASH();
      buf->numallbits = buf->allbits = 0;
    }
    buf->allbits <<= nbits;
    buf->allbits |= bits;
    buf->numallbits += nbits;
  }
}

size_t jxl_write_tokens_with_allotment(const jxl_token_stream* tokens,
                                const jxl_entropy_encoding_data* codes,
                                size_t context_offset, jxl_bit_writer* writer){
  size_t num_extra_bits = 0;
  if (codes->use_prefix_code) {
    for (size_t token_i = 0; token_i < jxl_array_len(tokens); ++token_i) {
      const jxl_token* token = jxl_array_at_const(tokens, token_i);
      uint32_t tok, nbits, bits;
      size_t histo = *jxl_array_at_const(&codes->context_map, context_offset + token->context);
      jxl_hybrid_uint_config_encode(
          (token->is_lz77_length ? codes->lz77.length_uint_config
                                : *jxl_array_at_const(&codes->uint_config, histo)),
          token->value, &tok, &nbits, &bits);
      tok += token->is_lz77_length ? codes->lz77.min_symbol : 0;
      // Combine two calls to the jxl_bit_writer. Equivalent to:
      // jxl_bit_writer_write(writer, jxl_entropy_encoding_data_symbol(codes, histo, tok)->depth,
      //               jxl_entropy_encoding_data_symbol(codes, histo, tok)->bits);
      // jxl_bit_writer_write(writer, nbits, bits);
      const jxl_ans_enc_symbol_info* info = jxl_entropy_encoding_data_symbol(codes, histo, tok);
      uint64_t data = info->bits;
      data |= (uint64_t)(bits) << info->depth;
      jxl_bit_writer_write(writer, info->depth + nbits, data);
      num_extra_bits += nbits;
    }
    return num_extra_bits;
  }
  jxl_memory_manager* mm = jxl_bit_writer_memory_manager(writer);
  jxl_array_u64 out;
  jxl_array_construct_empty(&out, mm);
  jxl_array_u8 out_nbits;
  jxl_array_construct_empty(&out_nbits, mm);
  if (!jxl_status_ok(jxl_array_reserve(&out, jxl_array_len(tokens)))) JXL_CRASH();
  if (!jxl_status_ok(jxl_array_reserve(&out_nbits, jxl_array_len(tokens)))) JXL_CRASH();
  // Writes in *reversed* order.
  jxl_ans_reversed_bit_buf bit_buf = {&out, &out_nbits, 0, 0};
  const int end = jxl_array_len(tokens);
  jxl_ans_coder ans;
  jxl_ans_coder_init(&ans);
  const uint16_t* reverse_maps = jxl_array_data_const(&codes->ans_reverse_maps);
  if (codes->lz77.enabled || jxl_array_len(&codes->context_map) > 1) {
    for (int i = end - 1; i >= 0; --i) {
      const jxl_token token = *jxl_array_at_const(tokens, i);
      const uint8_t histo = *jxl_array_at_const(&codes->context_map, context_offset + token.context);
      uint32_t tok, nbits, bits;
      jxl_hybrid_uint_config_encode(
          (token.is_lz77_length ? codes->lz77.length_uint_config
                               : *jxl_array_at_const(&codes->uint_config, histo)),
          token.value, &tok, &nbits, &bits);
      tok += token.is_lz77_length ? codes->lz77.min_symbol : 0;
      const jxl_ans_enc_symbol_info* info = jxl_entropy_encoding_data_symbol(codes, histo, tok);
      JXL_DASSERT(info->freq_ > 0);
      // Extra bits first as this is reversed.
      jxl_ans_reversed_bit_buf_add(&bit_buf, bits, nbits);
      num_extra_bits += nbits;
      uint8_t ans_nbits = 0;
      uint32_t ans_bits = jxl_ans_coder_put_symbol(&ans, info, reverse_maps, &ans_nbits);
      jxl_ans_reversed_bit_buf_add(&bit_buf, ans_bits, ans_nbits);
    }
  } else {
    for (int i = end - 1; i >= 0; --i) {
      uint32_t tok, nbits, bits;
      jxl_hybrid_uint_config_encode(*jxl_array_at_const(&codes->uint_config, 0), jxl_array_at_const(tokens, i)->value, &tok, &nbits, &bits);
      const jxl_ans_enc_symbol_info* info = jxl_entropy_encoding_data_symbol(codes, 0, tok);
      // Extra bits first as this is reversed.
      jxl_ans_reversed_bit_buf_add(&bit_buf, bits, nbits);
      num_extra_bits += nbits;
      uint8_t ans_nbits = 0;
      uint32_t ans_bits = jxl_ans_coder_put_symbol(&ans, info, reverse_maps, &ans_nbits);
      jxl_ans_reversed_bit_buf_add(&bit_buf, ans_bits, ans_nbits);
    }
  }
  const uint32_t state = jxl_ans_coder_get_state(&ans);
  jxl_bit_writer_write(writer, 32, state);
  jxl_bit_writer_write(writer, bit_buf.numallbits, bit_buf.allbits);
  for (int i = jxl_array_len(&out); i > 0; --i) {
    jxl_bit_writer_write(writer, *jxl_array_at(&out_nbits, i - 1), *jxl_array_at(&out, i - 1));
  }
  jxl_array_destroy(&out);
  jxl_array_destroy(&out_nbits);
  return num_extra_bits;
}

jxl_status jxl_write_tokens(const jxl_token_stream* tokens,
                   const jxl_entropy_encoding_data* codes, size_t context_offset,
                   jxl_bit_writer* writer, jxl_layer_type layer){
  // Theoretically, we could have 15 prefix code bits + 31 extra bits.
  jxl_write_tok_ctx ctx = {tokens, codes, context_offset, writer};
  return jxl_bit_writer_with_max_bits(writer, 46 * jxl_array_len(tokens) + 32 * 1024 * 4, layer,
                             jxl_write_tokens_body, &ctx);
}

jxl_status jxl_histogram_ans_population_cost(const jxl_histogram* h, const jxl_array_i32* counts,
                                  float* out) {
  return jxl_histogram_ans_population_cost_impl(h, counts, out);
}
