// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_ANS_H_
#define LIB_JXL_ENC_ANS_H_

// Library to encode the ANS population counts to the bit-stream and encode
// symbols based on the respective distributions.

#include "lib/jxl/memory_manager.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/jxl/ans_params.h"
#include "lib/jxl/base/array.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/dec_ans.h"
#include "lib/jxl/enc_ans_params.h"
#include "lib/jxl/enc_bit_writer.h"

#include "lib/jxl/layer_type.h"


#define USE_MULT_BY_RECIPROCAL

// precision must be equal to:  #bits(state_) + #bits(freq)
#define RECIPROCAL_PRECISION (32 + ANS_LOG_TAB_SIZE)

// Data structure representing one element of the encoding table built
// from a distribution. Trivially copyable: reverse maps live in a flat
// Array on jxl_entropy_encoding_data (indexed by reverse_map_offset).
typedef struct jxl_ans_enc_symbol_info {
  // ANS
  uint16_t freq_;
  // Start index into jxl_entropy_encoding_data::ans_reverse_maps (ANS only).
  uint32_t reverse_map_offset;
#ifdef USE_MULT_BY_RECIPROCAL
  uint64_t ifreq_;
#endif
  // Prefix coding.
  uint8_t depth;
  uint16_t bits;
} jxl_ans_enc_symbol_info;

JXL_DEFINE_POD_ARRAY(jxl_array_ans_enc_symbol_info, jxl_ans_enc_symbol_info)

typedef struct jxl_ans_coder {
  uint32_t state_;
} jxl_ans_coder;

static inline void jxl_ans_coder_init(jxl_ans_coder* self) {
  self->state_ = ANS_SIGNATURE << 16;
}

static inline uint32_t jxl_ans_coder_put_symbol(jxl_ans_coder* self, const jxl_ans_enc_symbol_info* t,
                                  const uint16_t* reverse_maps,
                                  uint8_t* nbits) {
  uint32_t bits = 0;
  *nbits = 0;
  if ((self->state_ >> (32 - ANS_LOG_TAB_SIZE)) >= t->freq_) {
    bits = self->state_ & 0xffff;
    self->state_ >>= 16;
    *nbits = 16;
  }
#ifdef USE_MULT_BY_RECIPROCAL
  // We use mult-by-reciprocal trick, but that requires 64b calc.
  const uint32_t v = (self->state_ * t->ifreq_) >> RECIPROCAL_PRECISION;
  const uint32_t offset =
      reverse_maps[t->reverse_map_offset + (self->state_ - v * t->freq_)];
  self->state_ = (v << ANS_LOG_TAB_SIZE) + offset;
#else
  self->state_ = ((self->state_ / t->freq_) << ANS_LOG_TAB_SIZE) +
                 reverse_maps[t->reverse_map_offset + (self->state_ % t->freq_)];
#endif
  return bits;
}

static inline uint32_t jxl_ans_coder_get_state(const jxl_ans_coder* self) { return self->state_; }


// Integer to be encoded by an entropy coder, either ANS or Huffman.
typedef struct jxl_token {
  uint32_t is_lz77_length : 1;
  uint32_t context : 31;
  uint32_t value;
} jxl_token;

static inline jxl_token jxl_token_make(uint32_t c, uint32_t value) {
  jxl_token t;
  t.is_lz77_length = false;
  t.context = c;
  t.value = value;
  return t;
}

JXL_DEFINE_POD_ARRAY(jxl_array_token, jxl_token)
typedef jxl_array_token jxl_token_stream;

// Move-only list of per-context/token streams (was MoveArray<jxl_token_stream>).
typedef struct jxl_token_streams {
  jxl_memory_manager* memory_manager;
  jxl_token_stream* ptr;
  size_t len;
  size_t capacity;

} jxl_token_streams;
static inline size_t jxl_token_streams_size(const jxl_token_streams* self) { return self->len; }
static inline bool jxl_token_streams_empty(const jxl_token_streams* self) { return self->len == 0; }
static inline jxl_token_stream* jxl_token_streams_data(jxl_token_streams* self) { return self->ptr; }
static inline const jxl_token_stream* jxl_token_streams_data_const(const jxl_token_streams* self) {
  return self->ptr;
}
static inline jxl_token_stream* jxl_token_streams_at(jxl_token_streams* self, size_t i) {
  return &self->ptr[i];
}
static inline const jxl_token_stream* jxl_token_streams_at_const(const jxl_token_streams* self,
                                                     size_t i) {
  return &self->ptr[i];
}

static inline void jxl_token_streams_construct_empty(jxl_token_streams* self) {
  self->memory_manager = NULL;
  self->ptr = NULL;
  self->len = 0;
  self->capacity = 0;
}

static inline void jxl_token_streams_clear(jxl_token_streams* self) {
  for (size_t i = 0; i < self->len; ++i) {
    jxl_array_destroy(self->ptr + i);
  }
  self->len = 0;
}

static inline jxl_status jxl_token_streams_reserve(jxl_token_streams* self,
                                         size_t new_capacity) {
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
  if (!jxl_safe_mul(grown, sizeof(jxl_token_stream), &bytes)) {
    return JXL_FAILURE("jxl_token_streams::reserve: size overflow");
  }
  jxl_token_stream* neu;
  if (self->memory_manager == NULL) {
    return JXL_FAILURE("jxl_token_streams::reserve: missing memory manager");
  }
  neu = (jxl_token_stream*)(
      self->memory_manager->alloc(self->memory_manager->opaque, bytes));
  if (neu == NULL) {
    return JXL_FAILURE("jxl_token_streams::reserve: allocation failed");
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

static inline jxl_status jxl_token_streams_resize(jxl_token_streams* self, size_t n) {
  if (n < self->len) {
    for (size_t i = n; i < self->len; ++i) {
      jxl_array_destroy(self->ptr + i);
    }
    self->len = n;
    return jxl_ok_status();
  }
  JXL_RETURN_IF_ERROR(jxl_token_streams_reserve(self, n));
  while (self->len < n) {
    jxl_array_construct_empty(self->ptr + self->len, self->memory_manager);
    ++self->len;
  }
  return jxl_ok_status();
}

static inline void jxl_token_streams_destroy(jxl_token_streams* self) {
  jxl_token_streams_clear(self);
  if (self->ptr != NULL) {
    if (self->memory_manager != NULL) {
      self->memory_manager->free(self->memory_manager->opaque, self->ptr);
    }
  }
  self->ptr = NULL;
  self->capacity = 0;
}

static inline void jxl_token_streams_create(jxl_token_streams* self, size_t n,
                                            jxl_memory_manager* mm) {
  jxl_token_streams_construct_empty(self);
  self->memory_manager = mm;
  if (!jxl_status_ok(jxl_token_streams_resize(self, n))) JXL_CRASH();
}

static inline jxl_status jxl_token_streams_push_back(jxl_token_streams* self,
                                          jxl_token_stream* value) {
  if (self->len == self->capacity) {
    size_t need;
    if (!jxl_safe_add(self->capacity, (size_t)(1), &need)) {
      return JXL_FAILURE("jxl_token_streams::push_back: overflow");
    }
    JXL_RETURN_IF_ERROR(jxl_token_streams_reserve(self, need));
  }
  jxl_array_construct_empty(self->ptr + self->len, self->memory_manager);
  jxl_array_swap(self->ptr + self->len, value);
  ++self->len;
  return jxl_ok_status();
}

static inline void jxl_token_streams_swap(jxl_token_streams* self, jxl_token_streams* other) {
  jxl_memory_manager* tmp_mm = self->memory_manager;
  self->memory_manager = other->memory_manager;
  other->memory_manager = tmp_mm;
  jxl_token_stream* tmp_ptr = self->ptr;
  self->ptr = other->ptr;
  other->ptr = tmp_ptr;
  jxl_swap(&self->len, &other->len);
  jxl_swap(&self->capacity, &other->capacity);
}



// jxl_hist_count_streams is defined in enc_ans_params.h.

typedef struct jxl_size_writer {
  size_t size;
} jxl_size_writer;
static inline void jxl_size_writer_construct_empty(jxl_size_writer* self) { self->size = 0; }
static inline void jxl_size_writer_write(jxl_size_writer* self, size_t num,
                                   size_t bits) {
  (void)bits;
  self->size += num;
}

// C-shaped bit sink so ANS helpers work with jxl_bit_writer or jxl_size_writer.
typedef struct jxl_bit_sink {
  void (*write)(void* opaque, size_t n_bits, uint64_t bits);
  void* opaque;
} jxl_bit_sink;

static inline void jxl_bit_sink_write(jxl_bit_sink* sink, size_t n_bits, uint64_t bits) {
  sink->write(sink->opaque, n_bits, bits);
}

static inline void jxl_size_writer_bit_sink_write(void* opaque, size_t n_bits,
                                          uint64_t bits) {
  (void)bits;
  ((jxl_size_writer*)(opaque))->size += n_bits;
}

static inline void jxl_bit_writer_bit_sink_write(void* opaque, size_t n_bits,
                                         uint64_t bits) {
  jxl_bit_writer_write((jxl_bit_writer*)(opaque), n_bits, bits);
}

static inline jxl_bit_sink jxl_make_size_writer_bit_sink(jxl_size_writer* writer) {
  jxl_bit_sink sink;
  sink.write = jxl_size_writer_bit_sink_write;
  sink.opaque = writer;
  return sink;
}

static inline jxl_bit_sink jxl_make_bit_writer_bit_sink(jxl_bit_writer* writer) {
  jxl_bit_sink sink;
  sink.write = jxl_bit_writer_bit_sink_write;
  sink.opaque = writer;
  return sink;
}

typedef struct jxl_entropy_encoding_data {
  // Flat symbol tables for all clustered histograms.
  jxl_array_ans_enc_symbol_info encoding_info;
  // encoding_info_starts[i] = start index of histogram i in encoding_info.
  jxl_array_u32 encoding_info_starts;
  // Flat ANS reverse maps; reverse_map_offset indexes into this.
  jxl_array_u16 ans_reverse_maps;
  bool use_prefix_code;
  jxl_array_hybrid_uint_config uint_config;
  size_t log_alpha_size;
  jxl_lz77_params lz77;
  jxl_array_u8 context_map;

} jxl_entropy_encoding_data;

jxl_status jxl_entropy_encoding_data_build_and_store_entropy_codes(
    jxl_entropy_encoding_data* self, jxl_memory_manager* memory_manager,
    const jxl_histogram_params* params, const jxl_token_streams* tokens,
    const jxl_array_histogram* builder, const jxl_hist_count_streams* builder_counts,
    jxl_bit_writer* writer, jxl_layer_type layer, size_t* cost_out);
jxl_status jxl_entropy_encoding_data_build_and_store_ans_encoding_data(
    jxl_entropy_encoding_data* self, jxl_memory_manager* memory_manager,
    jxl_ans_histogram_strategy ans_histogram_strategy, const jxl_histogram* histogram,
    const jxl_array_i32* counts, jxl_bit_writer* writer, size_t* cost_out);
jxl_status jxl_entropy_encoding_data_choose_uint_configs(
    jxl_entropy_encoding_data* self, jxl_memory_manager* memory_manager,
    const jxl_histogram_params* params, const jxl_token_streams* tokens,
    jxl_array_histogram* clustered_histograms, jxl_hist_count_streams* clustered_counts);

static inline void jxl_entropy_encoding_data_construct_empty(
    jxl_entropy_encoding_data* self, jxl_memory_manager* mm) {
  jxl_array_construct_empty(&self->encoding_info, mm);
  jxl_array_construct_empty(&self->encoding_info_starts, mm);
  jxl_array_construct_empty(&self->ans_reverse_maps, mm);
  self->use_prefix_code = false;
  jxl_array_construct_empty(&self->uint_config, mm);
  self->log_alpha_size = 0;
  jxl_lz77_params_init(&self->lz77);
  jxl_array_construct_empty(&self->context_map, mm);
}

static inline void jxl_entropy_encoding_data_destroy(jxl_entropy_encoding_data* self) {
  jxl_array_destroy(&self->encoding_info);
  jxl_array_destroy(&self->encoding_info_starts);
  jxl_array_destroy(&self->ans_reverse_maps);
  jxl_array_destroy(&self->uint_config);
  jxl_array_destroy(&self->context_map);
}

static inline void jxl_entropy_encoding_data_init(jxl_entropy_encoding_data* self,
                                                  jxl_memory_manager* mm) {
  jxl_entropy_encoding_data_construct_empty(self, mm);
}

static inline size_t jxl_entropy_encoding_data_num_histograms(
    const jxl_entropy_encoding_data* self) {
  return jxl_array_len(&self->encoding_info_starts);
}

static inline size_t jxl_entropy_encoding_data_alphabet_size(
    const jxl_entropy_encoding_data* self, size_t histo) {
  size_t begin = *jxl_array_at_const(&self->encoding_info_starts, histo);
  size_t end = (histo + 1 < jxl_array_len(&self->encoding_info_starts))
                   ? *jxl_array_at_const(&self->encoding_info_starts, histo + 1)
                   : jxl_array_len(&self->encoding_info);
  return end - begin;
}

static inline const jxl_ans_enc_symbol_info* jxl_entropy_encoding_data_symbol(
    const jxl_entropy_encoding_data* self, size_t histo, size_t tok) {
  return jxl_array_at_const(
      &self->encoding_info,
      *jxl_array_at_const(&self->encoding_info_starts, histo) + tok);
}
static inline void jxl_entropy_encoding_data_swap(jxl_entropy_encoding_data* self,
                                           jxl_entropy_encoding_data* other) {
  jxl_array_swap(&self->encoding_info, &other->encoding_info);
  jxl_array_swap(&self->encoding_info_starts, &other->encoding_info_starts);
  jxl_array_swap(&self->ans_reverse_maps, &other->ans_reverse_maps);
  bool tp = self->use_prefix_code;
  self->use_prefix_code = other->use_prefix_code;
  other->use_prefix_code = tp;
  jxl_array_swap(&self->uint_config, &other->uint_config);
  size_t tl = self->log_alpha_size;
  self->log_alpha_size = other->log_alpha_size;
  other->log_alpha_size = tl;
  jxl_lz77_params tlz = self->lz77;
  self->lz77 = other->lz77;
  other->lz77 = tlz;
  jxl_array_swap(&self->context_map, &other->context_map);
}


// Writes the context map to the bitstream.

// Apply context clustering, compute histograms and encode them. Returns an
// estimate of the total bits used for encoding the stream. If `writer` ==
// NULL, the bit estimate will not take into account the context map (which
// does not get written if `num_contexts` == 1).
// Returns cost
jxl_status jxl_build_and_encode_histograms(
    jxl_memory_manager* memory_manager, const jxl_histogram_params* params,
    size_t num_contexts, jxl_token_streams* tokens, jxl_entropy_encoding_data* codes,
    jxl_bit_writer* writer, jxl_layer_type layer, const jxl_array_size* image_widths,
    size_t* cost_out);

// Write the tokens to a string.
jxl_status jxl_write_tokens(const jxl_token_stream* tokens, const jxl_entropy_encoding_data* codes,
                   size_t context_offset, jxl_bit_writer* writer, jxl_layer_type layer);

// Same as jxl_write_tokens, but assumes allotment created by caller.
size_t jxl_write_tokens_with_allotment(const jxl_token_stream* tokens,
                                const jxl_entropy_encoding_data* codes,
                                size_t context_offset, jxl_bit_writer* writer);

void jxl_encode_uint_configs(const jxl_array_hybrid_uint_config* uint_config, jxl_bit_sink* sink,
                       size_t log_alpha_size);

#endif  // LIB_JXL_ENC_ANS_H_
