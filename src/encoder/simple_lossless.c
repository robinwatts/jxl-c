// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) the JPEG XL Project Authors. All rights reserved.
// The simple lossless encoder in this directory is also available under the
// BSD-style license in LICENSE-BSD and the additional patent grant in PATENTS.

#include "allocator.h"
#include <jxl/context.h>
#include "encoder/simple_lossless_internal.h"

#include <jxl/simple_lossless.h>

#include "base/bits.h"
#include "pack_signed.h"
#include "toc_size_params.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffU
#endif

#if defined(__GNUC__)
#define JXL_SL_ALIGN64 __attribute__((aligned(64)))
#else
#define JXL_SL_ALIGN64
#endif

#ifndef JXL_SL_MIN
#define JXL_SL_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef JXL_SL_MAX
#define JXL_SL_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#include <sys/types.h>

typedef struct BitDepthInfo {
  size_t bitdepth;
  size_t input_bytes;
  size_t num_symbols_ycocg;
  size_t num_symbols_plain;
  size_t max_encoded_bits_per_sample;
  const uint8_t *min_raw_length;
  const uint8_t *max_raw_length;
  size_t raw_length_table_size;
} BitDepthInfo;

static const uint8_t kUpTo8MinRaw[12] = {0};
static const uint8_t kUpTo8MaxRaw[12] = {
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 10,
};
static const uint8_t kFrom9To13MinRaw[17] = {0};
static const uint8_t kFrom9To13MaxRaw[17] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 10,
};
static const uint8_t kExactly14MinRaw[18] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 7,
};
static const uint8_t kExactly14MaxRaw[18] = {
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 10,
};
static const uint8_t kMoreThan14MinRaw[20] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 7,
};
static const uint8_t kMoreThan14MaxRaw[20] = {
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 10,
};

static BitDepthInfo get_bitdepth_info(size_t bitdepth) {
  BitDepthInfo info;
  if (bitdepth <= 8) {
    info.bitdepth = bitdepth;
    info.input_bytes = 1;
    info.num_symbols_ycocg = bitdepth + 3;
    info.num_symbols_plain = bitdepth + 2;
    info.max_encoded_bits_per_sample = 16;
    info.min_raw_length = kUpTo8MinRaw;
    info.max_raw_length = kUpTo8MaxRaw;
    info.raw_length_table_size = 12;
  } else if (bitdepth <= 13) {
    info.bitdepth = bitdepth;
    info.input_bytes = 2;
    info.num_symbols_ycocg = bitdepth + 3;
    info.num_symbols_plain = bitdepth + 2;
    info.max_encoded_bits_per_sample = 21;
    info.min_raw_length = kFrom9To13MinRaw;
    info.max_raw_length = kFrom9To13MaxRaw;
    info.raw_length_table_size = 17;
  } else if (bitdepth == 14) {
    info.bitdepth = 14;
    info.input_bytes = 2;
    info.num_symbols_ycocg = 17;
    info.num_symbols_plain = 17;
    info.max_encoded_bits_per_sample = 22;
    info.min_raw_length = kExactly14MinRaw;
    info.max_raw_length = kExactly14MaxRaw;
    info.raw_length_table_size = 18;
  } else {
    info.bitdepth = bitdepth;
    info.input_bytes = 2;
    info.num_symbols_ycocg = 19;
    info.num_symbols_plain = 19;
    info.max_encoded_bits_per_sample = 24;
    info.min_raw_length = kMoreThan14MinRaw;
    info.max_raw_length = kMoreThan14MaxRaw;
    info.raw_length_table_size = 20;
  }
  return info;
}

#define JXL_SL_MAX_FRAME_HEADER_SIZE 5
#define JXL_SL_NUM_RAW_SYMBOLS 19
#define JXL_SL_NUM_LZ77 33
#define JXL_SL_LZ77_CACHE_SIZE 32
#define JXL_SL_LZ77_OFFSET 224
#define JXL_SL_LZ77_MIN_LENGTH 7
#define JXL_SL_CHUNK_SIZE 8
#define JXL_SL_HASH_EXP 16
#define JXL_SL_HASH_SIZE 65536
#define JXL_SL_HASH_MULTIPLIER 2654435761U
#define JXL_SL_MAX_COLORS 512
#define JXL_SL_ALIGN_BYTES 64
#define JXL_SL_ROW_PADDING 32
#define JXL_SL_ROW_NUM_PX 384
#define JXL_SL_MAX_NUM_SYMBOLS 33
#define JXL_SL_MAX_CODE_LENGTH 15

typedef struct JxlChunkedFrameInputSource {
  void *opaque;
  const void *(*get_color_channel_data_at)(void *opaque, size_t xpos, size_t ypos,
                                           size_t xsize, size_t ysize, size_t *row_offset);
  void (*release_buffer)(void *opaque, const void *buf);
} JxlChunkedFrameInputSource;

typedef void (*JxlSlParallelRunner)(void *runner_opaque, void *opaque,
                                    void (*fun)(void *, size_t), size_t count);

typedef struct JxlSimpleLosslessFrameState JxlSimpleLosslessFrameState;

static void encode_hybrid_uint_lz77(uint32_t value, uint32_t *token, uint32_t *nbits,
                                    uint32_t *bits);
static void encode_hybrid_uint000(uint32_t value, uint32_t *token, uint32_t *nbits,
                                  uint32_t *bits);
static size_t toc_bucket(size_t group_size);

static void jxl_sl_prepare_header(JxlSimpleLosslessFrameState *frame, int add_image_header,
                                    int is_last);

static size_t jxl_sl_max_required_output(const JxlSimpleLosslessFrameState *frame);
static size_t jxl_sl_output_size(const JxlSimpleLosslessFrameState *frame);

static size_t jxl_sl_write_output(JxlSimpleLosslessFrameState *frame, unsigned char *output,
                                    size_t output_size);

static void jxl_sl_free_frame_state(JxlSimpleLosslessFrameState *frame);

static size_t bitdepth_num_symbols(const BitDepthInfo *bd, int doing_ycocg_or_large_palette) {
  return doing_ycocg_or_large_palette ? bd->num_symbols_ycocg : bd->num_symbols_plain;
}

#if defined(_MSC_VER) && !defined(__clang__)
#define FJXL_INLINE static __forceinline
FJXL_INLINE uint32_t ctz_non_zero(uint64_t v) {
  unsigned long index;
  _BitScanForward(&index, v);
  return index;
}
#else
#define FJXL_INLINE static __attribute__((always_inline))
FJXL_INLINE uint32_t ctz_non_zero(uint64_t v) { return __builtin_ctzll(v); }
#endif

FJXL_INLINE void store_le64(uint8_t *tgt, uint64_t data) {
#if (!defined(__BYTE_ORDER__) || (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__))
  int i;
  for (i = 0; i < 8; i++) {
    tgt[i] = (data >> (i * 8)) & 0xFF;
  }
#else
  memcpy(tgt, &data, 8);
#endif
}

FJXL_INLINE size_t add_bits(uint32_t count, uint64_t bits, uint8_t *data_buf,
                            size_t *bits_in_buffer, uint64_t *bit_buffer) {
  size_t bytes_in_buffer;
  *bit_buffer |= bits << (*bits_in_buffer);
  (*bits_in_buffer) += count;
  store_le64(data_buf, *bit_buffer);
  bytes_in_buffer = (*bits_in_buffer) / 8;
  (*bits_in_buffer) -= bytes_in_buffer * 8;
  *bit_buffer >>= bytes_in_buffer * 8;
  return bytes_in_buffer;
}

typedef struct BitWriter {
  uint8_t *data;
  size_t bytes_written;
  size_t bits_in_buffer;
  uint64_t buffer;
} BitWriter;

typedef struct BitWriterGroup {
  BitWriter writers[4];
} BitWriterGroup;

static void bit_writer_allocate(jxl_context *alloc, BitWriter *writer,
                                size_t maximum_bit_size) {
  assert(writer->data == NULL);
  writer->data = (uint8_t *)jxl_alloc(alloc, maximum_bit_size / 8 + 64);
  if (writer->data == NULL) {
    abort();
  }
}

static void bit_writer_write(BitWriter *writer, uint32_t count, uint64_t bits) {
  writer->bytes_written += add_bits(count, bits, writer->data + writer->bytes_written,
                                    &writer->bits_in_buffer, &writer->buffer);
}

static void bit_writer_zero_pad_to_byte(BitWriter *writer) {
  if (writer->bits_in_buffer != 0) {
    bit_writer_write(writer, 8 - writer->bits_in_buffer, 0);
  }
}

FJXL_INLINE void bit_writer_write_multiple(BitWriter *writer, const uint64_t *nbits,
                                           const uint64_t *bits, size_t n) {
  size_t i;
  uint64_t shift;
  size_t bytes_in_buffer;
  for (i = 0; i < n; i++) {
    writer->buffer |= bits[i] << writer->bits_in_buffer;
    memcpy(writer->data + writer->bytes_written, &writer->buffer, 8);
    shift = 64 - writer->bits_in_buffer;
    writer->bits_in_buffer += nbits[i];
    if (writer->bits_in_buffer >= 64) {
      uint64_t next_buffer = shift >= 64 ? 0 : bits[i] >> shift;
      writer->buffer = next_buffer;
      writer->bits_in_buffer -= 64;
      writer->bytes_written += 8;
    }
  }
  memcpy(writer->data + writer->bytes_written, &writer->buffer, 8);
  bytes_in_buffer = writer->bits_in_buffer / 8;
  writer->bits_in_buffer -= bytes_in_buffer * 8;
  writer->buffer >>= bytes_in_buffer * 8;
  writer->bytes_written += bytes_in_buffer;
}

static void bit_writer_free(jxl_context *alloc, BitWriter *writer) {
  jxl_free(alloc, writer->data);
  writer->data = NULL;
}

static size_t section_size(const BitWriterGroup *group_data) {
  size_t sz = 0;
  size_t j;
  for (j = 0; j < 4; j++) {
    const BitWriter *writer = &group_data->writers[j];
    sz += writer->bytes_written * 8 + writer->bits_in_buffer;
  }
  sz = (sz + 7) / 8;
  return sz;
}


typedef struct PrefixCode {
  uint8_t raw_nbits[19]; /* JXL_SL_NUM_RAW_SYMBOLS */
  uint8_t raw_bits[19];
  uint8_t lz77_nbits[33]; /* JXL_SL_NUM_LZ77 */
  uint16_t lz77_bits[33];
  uint64_t lz77_cache_bits[32]; /* JXL_SL_LZ77_CACHE_SIZE */
  uint8_t lz77_cache_nbits[32];
  size_t numraw;
} PrefixCode;

static uint16_t prefix_code_bit_reverse(size_t nbits, uint16_t bits) {
  static const uint16_t kNibbleLookup[16] = {
      0, 8, 4, 12, 2, 10, 6, 14,
      1, 9, 5, 13, 3, 11, 7, 15,
  };
  uint16_t rev16 = (kNibbleLookup[bits & 0xF] << 12) |
                   (kNibbleLookup[(bits >> 4) & 0xF] << 8) |
                   (kNibbleLookup[(bits >> 8) & 0xF] << 4) |
                   (kNibbleLookup[bits >> 12]);
  return rev16 >> (16 - nbits);
}

#define JXL_SL_MAX_CODE_LENGTH 15

static void prefix_code_compute_canonical_code(const uint8_t *first_chunk_nbits,
                                               uint8_t *first_chunk_bits,
                                               size_t first_chunk_size,
                                               const uint8_t *second_chunk_nbits,
                                               uint16_t *second_chunk_bits,
                                               size_t second_chunk_size) {
  uint8_t code_length_counts[16];
  uint16_t next_code[16];
  uint16_t code;
  size_t i;

  memset(code_length_counts, 0, sizeof(code_length_counts));
  memset(next_code, 0, sizeof(next_code));
  for (i = 0; i < first_chunk_size; i++) {
    code_length_counts[first_chunk_nbits[i]]++;
    assert(first_chunk_nbits[i] <= JXL_SL_MAX_CODE_LENGTH);
    assert(first_chunk_nbits[i] <= 8);
    assert(first_chunk_nbits[i] > 0);
  }
  for (i = 0; i < second_chunk_size; i++) {
    code_length_counts[second_chunk_nbits[i]]++;
    assert(second_chunk_nbits[i] <= JXL_SL_MAX_CODE_LENGTH);
  }
  code = 0;
  for (i = 1; i < JXL_SL_MAX_CODE_LENGTH + 1; i++) {
    code = (code + code_length_counts[i - 1]) << 1;
    next_code[i] = code;
  }
  for (i = 0; i < first_chunk_size; i++) {
    first_chunk_bits[i] =
        (uint8_t)prefix_code_bit_reverse(first_chunk_nbits[i], next_code[first_chunk_nbits[i]]++);
  }
  for (i = 0; i < second_chunk_size; i++) {
    second_chunk_bits[i] =
        prefix_code_bit_reverse(second_chunk_nbits[i], next_code[second_chunk_nbits[i]]++);
  }
}

static void prefix_code_compute_code_lengths_nonzero_impl_u32(
    jxl_context *alloc, const uint64_t *freqs, size_t n, size_t precision, uint32_t infty,
    const uint8_t *min_limit, const uint8_t *max_limit, uint8_t *nbits) {
  size_t table_size = ((1U << precision) + 1) * (n + 1);
  uint32_t *dynp = (uint32_t *)jxl_alloc(alloc, table_size * sizeof(uint32_t));
  size_t i, sym, off;
  for (i = 0; i < table_size; i++) dynp[i] = infty;
#define D(sym, off) dynp[(sym) * ((1 << precision) + 1) + (off)]
  D(0, 0) = 0;
  for (sym = 0; sym < n; sym++) {
    uint32_t bits;
    for (bits = min_limit[sym]; bits <= max_limit[sym]; bits++) {
      size_t off_delta = 1U << (precision - bits);
      for (off = 0; off + off_delta <= (1U << precision); off++) {
        uint32_t candidate = D(sym, off) + (uint32_t)(freqs[sym] * bits);
        if (candidate < D(sym + 1, off + off_delta)) {
          D(sym + 1, off + off_delta) = candidate;
        }
      }
    }
  }
  sym = n;
  off = 1U << precision;
  assert(D(sym, off) != infty);
  while (sym-- > 0) {
    uint32_t bits;
    assert(off > 0);
    for (bits = min_limit[sym]; bits <= max_limit[sym]; bits++) {
      size_t off_delta = 1U << (precision - bits);
      if (off_delta <= off &&
          D(sym + 1, off) == D(sym, off - off_delta) + (uint32_t)(freqs[sym] * bits)) {
        off -= off_delta;
        nbits[sym] = (uint8_t)bits;
        break;
      }
    }
  }
#undef D
  jxl_free(alloc, dynp);
}

static void prefix_code_compute_code_lengths_nonzero_impl_u64(
    jxl_context *alloc, const uint64_t *freqs, size_t n, size_t precision, uint64_t infty,
    const uint8_t *min_limit, const uint8_t *max_limit, uint8_t *nbits) {
  size_t table_size = ((1U << precision) + 1) * (n + 1);
  uint64_t *dynp = (uint64_t *)jxl_alloc(alloc, table_size * sizeof(uint64_t));
  size_t i, sym, off;
  for (i = 0; i < table_size; i++) dynp[i] = infty;
#define D64(sym, off) dynp[(sym) * ((1 << precision) + 1) + (off)]
  D64(0, 0) = 0;
  for (sym = 0; sym < n; sym++) {
    uint64_t bits;
    for (bits = min_limit[sym]; bits <= max_limit[sym]; bits++) {
      size_t off_delta = 1U << (precision - bits);
      for (off = 0; off + off_delta <= (1U << precision); off++) {
        uint64_t candidate = D64(sym, off) + freqs[sym] * bits;
        if (candidate < D64(sym + 1, off + off_delta)) {
          D64(sym + 1, off + off_delta) = candidate;
        }
      }
    }
  }
  sym = n;
  off = 1U << precision;
  assert(D64(sym, off) != infty);
  while (sym-- > 0) {
    uint64_t bits;
    assert(off > 0);
    for (bits = min_limit[sym]; bits <= max_limit[sym]; bits++) {
      size_t off_delta = 1U << (precision - bits);
      if (off_delta <= off &&
          D64(sym + 1, off) == D64(sym, off - off_delta) + freqs[sym] * bits) {
        off -= off_delta;
        nbits[sym] = (uint8_t)bits;
        break;
      }
    }
  }
#undef D64
  jxl_free(alloc, dynp);
}

static void prefix_code_compute_code_lengths_nonzero(jxl_context *alloc, const uint64_t *freqs, size_t n,
                                                     uint8_t *min_limit, uint8_t *max_limit,
                                                     uint8_t *nbits) {
  size_t precision = 0;
  size_t shortest_length = 255;
  uint64_t freqsum = 0;
  size_t i;
  for (i = 0; i < n; i++) {
    assert(freqs[i] != 0);
    freqsum += freqs[i];
    if (min_limit[i] < 1) min_limit[i] = 1;
    assert(min_limit[i] <= max_limit[i]);
    precision = JXL_SL_MAX(max_limit[i], precision);
    shortest_length = JXL_SL_MIN(min_limit[i], shortest_length);
  }
  precision -= shortest_length - 1;
  uint64_t infty = freqsum * precision;
  if (infty < UINT32_MAX / 2) {
    prefix_code_compute_code_lengths_nonzero_impl_u32(
        alloc, freqs, n, precision, (uint32_t)infty, min_limit, max_limit, nbits);
  } else {
    prefix_code_compute_code_lengths_nonzero_impl_u64(
        alloc, freqs, n, precision, infty, min_limit, max_limit, nbits);
  }
}

static void prefix_code_compute_code_lengths(jxl_context *alloc, const uint64_t *freqs, size_t n,
                                             const uint8_t *min_limit_in,
                                             const uint8_t *max_limit_in,
                                             uint8_t *nbits) {
  assert(n <= JXL_SL_MAX_NUM_SYMBOLS);
  uint64_t compact_freqs[33];
  uint8_t min_limit[33];
  uint8_t max_limit[33];
  size_t ni = 0;
  size_t i;
  for (i = 0; i < n; i++) {
    if (freqs[i]) {
      compact_freqs[ni] = freqs[i];
      min_limit[ni] = min_limit_in[i];
      max_limit[ni] = max_limit_in[i];
      ni++;
    }
  }
  for (i = ni; i < JXL_SL_MAX_NUM_SYMBOLS; ++i) {
    compact_freqs[i] = 0;
    min_limit[i] = 0;
    max_limit[i] = 0;
  }
  uint8_t num_bits[33] = {0};
  prefix_code_compute_code_lengths_nonzero(alloc, compact_freqs, ni, min_limit, max_limit, num_bits);
  ni = 0;
  for (i = 0; i < n; i++) {
    nbits[i] = 0;
    if (freqs[i]) {
      nbits[i] = num_bits[ni++];
    }
  }
}

static void prefix_code_init(jxl_context *alloc, PrefixCode *code, const BitDepthInfo *bd,
                             uint64_t *raw_counts, uint64_t *lz77_counts) {
  uint64_t level1_counts[20];
  size_t i;
  size_t count;
  size_t num_lz77;
  uint8_t level1_nbits[20];
  uint8_t level2_nbits[33];
  uint8_t min_lengths[33];
  uint8_t l;
  uint8_t max_lengths[33];
  unsigned token, nbits, bits;

  memcpy(level1_counts, raw_counts, 19 * sizeof(uint64_t));
  code->numraw = 19;
  while (code->numraw > 0 && level1_counts[code->numraw - 1] == 0) code->numraw--;
  level1_counts[code->numraw] = 0;
  for (i = 0; i < 33; i++) {
    level1_counts[code->numraw] += lz77_counts[i];
  }
  memset(level1_nbits, 0, sizeof(level1_nbits));
  prefix_code_compute_code_lengths(alloc, level1_counts, code->numraw + 1,
                                   bd->min_raw_length, bd->max_raw_length, level1_nbits);
  memset(level2_nbits, 0, sizeof(level2_nbits));
  memset(min_lengths, 0, sizeof(min_lengths));
  l = (uint8_t)(15 - level1_nbits[code->numraw]);
  for (i = 0; i < 33; i++) max_lengths[i] = l;
  num_lz77 = 33;
  while (num_lz77 > 0 && lz77_counts[num_lz77 - 1] == 0) num_lz77--;
  prefix_code_compute_code_lengths(alloc, lz77_counts, num_lz77, min_lengths, max_lengths, level2_nbits);
  for (i = 0; i < code->numraw; i++) code->raw_nbits[i] = level1_nbits[i];
  for (i = 0; i < num_lz77; i++) {
    code->lz77_nbits[i] = level2_nbits[i] ? (uint8_t)(level1_nbits[code->numraw] + level2_nbits[i]) : 0;
  }
  prefix_code_compute_canonical_code(code->raw_nbits, code->raw_bits, code->numraw,
                                     code->lz77_nbits, code->lz77_bits, 33);
  for (count = 0; count < 32; count++) {
    encode_hybrid_uint_lz77((uint32_t)count, &token, &nbits, &bits);
    code->lz77_cache_nbits[count] = (uint8_t)(code->lz77_nbits[token] + nbits + code->raw_nbits[0]);
    code->lz77_cache_bits[count] =
        (((bits << code->lz77_nbits[token]) | code->lz77_bits[token]) << code->raw_nbits[0]) |
        code->raw_bits[0];
  }
}

static void prefix_code_write_to(jxl_context *alloc, const PrefixCode *code,
                                 BitWriter *writer) {
  uint64_t code_length_counts[18] = {0};
  code_length_counts[17] = 3 + 2 * (33 - 1);
  size_t i;
  for (i = 0; i < 19; i++) code_length_counts[code->raw_nbits[i]]++;
  for (i = 0; i < 33; i++) code_length_counts[code->lz77_nbits[i]]++;
  uint8_t code_length_nbits[18] = {0};
  uint8_t code_length_nbits_min[18] = {0};
  uint8_t code_length_nbits_max[18] = {
      5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  };
  prefix_code_compute_code_lengths(alloc, code_length_counts, 18, code_length_nbits_min,
                                   code_length_nbits_max, code_length_nbits);
  bit_writer_write(writer, 2, 0);
  static const uint8_t code_length_order[18] = {1, 2, 3, 4,  0,  5,  17, 6,  16,
                                                7, 8, 9, 10, 11, 12, 13, 14, 15};
  static const uint8_t code_length_length_nbits[] = {2, 4, 3, 2, 2, 4};
  static const uint8_t code_length_length_bits[] = {0, 7, 3, 2, 1, 15};
  size_t num_code_lengths = 18;
  while (code_length_nbits[code_length_order[num_code_lengths - 1]] == 0) num_code_lengths--;
  for (i = 0; i < num_code_lengths; i++) {
    int symbol = code_length_nbits[code_length_order[i]];
    bit_writer_write(writer, code_length_length_nbits[symbol], code_length_length_bits[symbol]);
  }
  uint16_t code_length_bits[18] = {0};
  prefix_code_compute_canonical_code(NULL, NULL, 0, code_length_nbits, code_length_bits, 18);
  for (i = 0; i < 19; i++) {
    bit_writer_write(writer, code_length_nbits[code->raw_nbits[i]], code_length_bits[code->raw_nbits[i]]);
  }
  size_t num_lz77 = 33;
  while (code->lz77_nbits[num_lz77 - 1] == 0) num_lz77--;
  bit_writer_write(writer, code_length_nbits[17], code_length_bits[17]);
  bit_writer_write(writer, 3, 2);
  bit_writer_write(writer, code_length_nbits[17], code_length_bits[17]);
  bit_writer_write(writer, 3, 0);
  bit_writer_write(writer, code_length_nbits[17], code_length_bits[17]);
  bit_writer_write(writer, 3, 2);
  for (i = 0; i < num_lz77; i++) {
    bit_writer_write(writer, code_length_nbits[code->lz77_nbits[i]], code_length_bits[code->lz77_nbits[i]]);
  }
}


struct JxlSimpleLosslessFrameState {
  jxl_context *alloc;
  JxlChunkedFrameInputSource input;
  size_t width;
  size_t height;
  size_t num_groups_x;
  size_t num_groups_y;
  size_t num_dc_groups_x;
  size_t num_dc_groups_y;
  size_t nb_chans;
  size_t bitdepth;
  int big_endian;
  int effort;
  int collided;
  PrefixCode hcode[4];
  int16_t *lookup;
  BitWriter header;
  BitWriterGroup *group_data;
  size_t num_groups;
  size_t *group_sizes;
  size_t ac_group_data_offset;
  size_t min_dc_global_size;
  size_t current_bit_writer;
  size_t bit_writer_byte_pos;
  size_t bits_in_buffer;
  uint64_t bit_buffer;
  int process_done;
};



static size_t jxl_sl_output_size(const JxlSimpleLosslessFrameState *frame) {
  size_t total_size_groups = 0;
  size_t g;
  for (g = 0; g < frame->num_groups; g++) {
    total_size_groups += section_size(&frame->group_data[g]);
  }
  return frame->header.bytes_written + total_size_groups;
}

static size_t jxl_sl_max_required_output(const JxlSimpleLosslessFrameState *frame) {
  return jxl_sl_output_size(frame) + 32;
}

static void header_write_size(BitWriter *output, size_t size) {
  if (size - 1 < (1 << 9)) {
    bit_writer_write(output, 2, 0);
    bit_writer_write(output, 9, size - 1);
  } else if (size - 1 < (1 << 13)) {
    bit_writer_write(output, 2, 1);
    bit_writer_write(output, 13, size - 1);
  } else if (size - 1 < (1 << 18)) {
    bit_writer_write(output, 2, 2);
    bit_writer_write(output, 18, size - 1);
  } else {
    bit_writer_write(output, 2, 3);
    bit_writer_write(output, 30, size - 1);
  }
}

static void jxl_sl_prepare_header(JxlSimpleLosslessFrameState *frame,
                                    int add_image_header, int is_last) {
  BitWriter *output = &frame->header;
  bit_writer_allocate(frame->alloc, output, 1000 + frame->num_groups * 32);
  int have_alpha = (frame->nb_chans == 2 || frame->nb_chans == 4);
  if (add_image_header) {
    bit_writer_write(output, 16, 0x0AFF);
    bit_writer_write(output, 1, 0);
    header_write_size(output, frame->height);
    bit_writer_write(output, 3, 0);
    header_write_size(output, frame->width);
    bit_writer_write(output, 1, 0);
    bit_writer_write(output, 1, 0);
    bit_writer_write(output, 1, 0);
    if (frame->bitdepth == 8) {
      bit_writer_write(output, 2, 0);
    } else if (frame->bitdepth == 10) {
      bit_writer_write(output, 2, 1);
    } else if (frame->bitdepth == 12) {
      bit_writer_write(output, 2, 2);
    } else {
      bit_writer_write(output, 2, 3);
      bit_writer_write(output, 6, frame->bitdepth - 1);
    }
    if (frame->bitdepth <= 14) {
      bit_writer_write(output, 1, 1);
    } else {
      bit_writer_write(output, 1, 0);
    }
    if (have_alpha) {
      bit_writer_write(output, 2, 1);
      if (frame->bitdepth == 8) {
        bit_writer_write(output, 1, 1);
      } else {
        bit_writer_write(output, 1, 0);
        bit_writer_write(output, 2, 0);
        bit_writer_write(output, 1, 0);
        if (frame->bitdepth == 10) {
          bit_writer_write(output, 2, 1);
        } else if (frame->bitdepth == 12) {
          bit_writer_write(output, 2, 2);
        } else {
          bit_writer_write(output, 2, 3);
          bit_writer_write(output, 6, frame->bitdepth - 1);
        }
        bit_writer_write(output, 2, 0);
        bit_writer_write(output, 2, 0);
        bit_writer_write(output, 1, 0);
      }
    } else {
      bit_writer_write(output, 2, 0);
    }
    bit_writer_write(output, 1, 0);
    if (frame->nb_chans > 2) {
      bit_writer_write(output, 1, 1);
    } else {
      bit_writer_write(output, 1, 0);
      bit_writer_write(output, 1, 0);
      bit_writer_write(output, 2, 1);
      bit_writer_write(output, 2, 1);
      bit_writer_write(output, 1, 0);
      bit_writer_write(output, 2, 2);
      bit_writer_write(output, 4, 11);
      bit_writer_write(output, 2, 1);
    }
    bit_writer_write(output, 2, 0);
    bit_writer_write(output, 1, 1);
    bit_writer_zero_pad_to_byte(output);
  }
  bit_writer_write(output, 1, 0);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 1, 0);
  bit_writer_write(output, 2, 0);
  if (have_alpha) bit_writer_write(output, 2, 0);
  bit_writer_write(output, 2, 1);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 1, 0);
  bit_writer_write(output, 2, 0);
  if (have_alpha) bit_writer_write(output, 2, 0);
  bit_writer_write(output, 1, is_last);
  if (!is_last) bit_writer_write(output, 2, 0);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 1, 0);
  bit_writer_write(output, 1, 0);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 1, 0);
  bit_writer_zero_pad_to_byte(output);
  assert(add_image_header || output->bytes_written <= JXL_SL_MAX_FRAME_HEADER_SIZE);
  {
    size_t gi;
    for (gi = 0; gi < frame->num_groups; gi++) {
      size_t group_size = frame->group_sizes[gi];
      size_t bucket = toc_bucket(group_size);
      bit_writer_write(output, 2, bucket);
      bit_writer_write(output, (uint32_t)jxl_toc_group_size_extra_bits[bucket],
                       group_size - jxl_toc_group_size_offset[bucket]);
    }
  }
  bit_writer_zero_pad_to_byte(output);
}

static size_t write_bits_helper(size_t num, uint64_t bits, unsigned char **output,
                                size_t *output_size, size_t *bits_in_buffer,
                                uint64_t *bit_buffer) {
  size_t n = add_bits((uint32_t)num, bits, *output, bits_in_buffer, bit_buffer);
  *output += n;
  *output_size -= n;
  return n;
}

static size_t jxl_sl_write_output(JxlSimpleLosslessFrameState *frame,
                                    unsigned char *output, size_t output_size) {
  assert(output_size >= 32);
  unsigned char *initial_output = output;
  while (1) {
    size_t *cur = &frame->current_bit_writer;
    size_t *bw_pos = &frame->bit_writer_byte_pos;
    if (*cur >= 1 + frame->num_groups * frame->nb_chans) {
      return (size_t)(output - initial_output);
    }
    if (output_size <= 9) {
      return (size_t)(output - initial_output);
    }
    size_t nbc = frame->nb_chans;
    const BitWriter *writer =
        *cur == 0 ? &frame->header
                  : &frame->group_data[(*cur - 1) / nbc].writers[(*cur - 1) % nbc];
    size_t full_byte_count = JXL_SL_MIN(output_size - 9, writer->bytes_written - *bw_pos);
    if (frame->bits_in_buffer == 0) {
      memcpy(output, writer->data + *bw_pos, full_byte_count);
    } else {
      size_t i = 0;
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
      for (; i + 8 < full_byte_count; i += 8) {
        uint64_t chunk;
        memcpy(&chunk, writer->data + *bw_pos + i, 8);
        uint64_t out = frame->bit_buffer | (chunk << frame->bits_in_buffer);
        memcpy(output + i, &out, 8);
        frame->bit_buffer = chunk >> (64 - frame->bits_in_buffer);
      }
#endif
      for (; i < full_byte_count; i++) {
        add_bits(8, writer->data[*bw_pos + i], output + i, &frame->bits_in_buffer,
                 &frame->bit_buffer);
      }
    }
    output += full_byte_count;
    output_size -= full_byte_count;
    *bw_pos += full_byte_count;
    if (*bw_pos == writer->bytes_written) {
      if (writer->bits_in_buffer) {
        write_bits_helper(writer->bits_in_buffer, writer->buffer, &output, &output_size,
                          &frame->bits_in_buffer, &frame->bit_buffer);
      }
      *bw_pos = 0;
      (*cur)++;
      if ((*cur - 1) % nbc == 0 && frame->bits_in_buffer != 0) {
        write_bits_helper(8 - frame->bits_in_buffer, 0, &output, &output_size,
                          &frame->bits_in_buffer, &frame->bit_buffer);
      }
    }
  }
}

static void frame_state_free_all(JxlSimpleLosslessFrameState *frame) {
  size_t g, c;
  bit_writer_free(frame->alloc, &frame->header);
  if (frame->group_data) {
    for (g = 0; g < frame->num_groups; g++) {
      for (c = 0; c < 4; c++) {
        bit_writer_free(frame->alloc, &frame->group_data[g].writers[c]);
      }
    }
    jxl_free(frame->alloc, frame->group_data);
  }
  jxl_free(frame->alloc, frame->group_sizes);
  jxl_free(frame->alloc, frame->lookup);
  jxl_free(frame->alloc, frame);
}

static void jxl_sl_free_frame_state(JxlSimpleLosslessFrameState *frame) {
  frame_state_free_all(frame);
}

void encode_hybrid_uint000(uint32_t value, uint32_t *token, uint32_t *nbits,
                           uint32_t *bits) {
  uint32_t n = value ? (uint32_t)jxl_floor_log2_nonzero32(value) : 0;
  *token = value ? n + 1 : 0;
  *nbits = value ? n : 0;
  *bits = value ? value - (1U << n) : 0;
}

void encode_hybrid_uint_lz77(uint32_t value, uint32_t *token, uint32_t *nbits,
                             uint32_t *bits) {
  uint32_t n = value ? (uint32_t)jxl_floor_log2_nonzero32(value) : 0;
  *token = value < 16 ? value : 16 + n - 4;
  *nbits = value < 16 ? 0 : n;
  *bits = value < 16 ? 0 : value - (1U << *nbits);
}

size_t toc_bucket(size_t group_size) {
  size_t bucket = 0;
  while (bucket < 3 && group_size >= jxl_toc_group_size_offset[bucket + 1]) ++bucket;
  return bucket;
}

void compute_ac_group_data_offset(size_t dc_global_size, size_t num_dc_groups,
                                  size_t num_ac_groups, size_t *min_dc_global_size,
                                  size_t *ac_group_offset) {
  size_t ac_toc_max_bits = num_ac_groups * 24;
  size_t ac_toc_min_bits = num_ac_groups * 12;
  size_t max_padding = 1 + (ac_toc_max_bits - ac_toc_min_bits + 7) / 8;
  *min_dc_global_size = dc_global_size;
  size_t dc_global_bucket = toc_bucket(*min_dc_global_size);
  while (toc_bucket(*min_dc_global_size + max_padding) > dc_global_bucket) {
    dc_global_bucket = toc_bucket(*min_dc_global_size + max_padding);
    *min_dc_global_size = jxl_toc_group_size_offset[dc_global_bucket];
  }
  assert(toc_bucket(*min_dc_global_size) == dc_global_bucket);
  assert(toc_bucket(*min_dc_global_size + max_padding) == dc_global_bucket);
  size_t max_toc_bits = 2 + jxl_toc_group_size_extra_bits[dc_global_bucket] +
                        12 * (1 + num_dc_groups) + ac_toc_max_bits;
  size_t max_toc_size = (max_toc_bits + 7) / 8;
  *ac_group_offset = JXL_SL_MAX_FRAME_HEADER_SIZE + max_toc_size + *min_dc_global_size;
}

static void chunk_encoder_encode_rle(size_t count, const PrefixCode *code, BitWriter *output) {
  if (count == 0) return;
  count -= JXL_SL_LZ77_MIN_LENGTH + 1;
  if (count < JXL_SL_LZ77_CACHE_SIZE) {
    bit_writer_write(output, code->lz77_cache_nbits[count], code->lz77_cache_bits[count]);
  } else {
    unsigned token, nbits, bits;
    encode_hybrid_uint_lz77((uint32_t)count, &token, &nbits, &bits);
    uint64_t wbits = bits;
    wbits = (wbits << code->lz77_nbits[token]) | code->lz77_bits[token];
    wbits = (wbits << code->raw_nbits[0]) | code->raw_bits[0];
    bit_writer_write(output, code->lz77_nbits[token] + nbits + code->raw_nbits[0], wbits);
  }
}

static void generic_encode_chunk(const uint32_t *residuals, size_t n, size_t skip,
                                 const PrefixCode *code, BitWriter *output) {
  size_t ix;
  for (ix = skip; ix < n; ix++) {
    unsigned token, nbits, bits;
    encode_hybrid_uint000(residuals[ix], &token, &nbits, &bits);
    bit_writer_write(output, code->raw_nbits[token] + nbits,
                     code->raw_bits[token] | ((uint64_t)bits << code->raw_nbits[token]));
  }
}

typedef struct ChunkEncoder {
  const PrefixCode *code;
  BitWriter *output;
} ChunkEncoder;

static void chunk_encoder_chunk(ChunkEncoder *enc, size_t run, uint32_t *residuals,
                                size_t skip, size_t n) {
  chunk_encoder_encode_rle(run, enc->code, enc->output);
  generic_encode_chunk(residuals, n, skip, enc->code, enc->output);
}

static void chunk_encoder_finalize(ChunkEncoder *enc, size_t run) {
  chunk_encoder_encode_rle(run, enc->code, enc->output);
}

typedef struct ChunkSampleCollector {
  uint64_t *raw_counts;
  uint64_t *lz77_counts;
} ChunkSampleCollector;

static void sample_collector_rle(ChunkSampleCollector *col, size_t count) {
  if (count == 0) return;
  col->raw_counts[0] += 1;
  count -= JXL_SL_LZ77_MIN_LENGTH + 1;
  unsigned token, nbits, bits;
  encode_hybrid_uint_lz77((uint32_t)count, &token, &nbits, &bits);
  col->lz77_counts[token]++;
  (void)nbits;
  (void)bits;
}

static void sample_collector_chunk(ChunkSampleCollector *col, size_t run,
                                   const uint32_t *residuals, size_t skip, size_t n) {
  sample_collector_rle(col, run);
  size_t ix;
  for (ix = skip; ix < n; ix++) {
    unsigned token, nbits, bits;
    encode_hybrid_uint000(residuals[ix], &token, &nbits, &bits);
    col->raw_counts[token]++;
  }
}

static void sample_collector_finalize(ChunkSampleCollector *col, size_t run) {
  (void)col;
  (void)run;
}

typedef struct ChannelRowProcessor {
  void *ctx;
  void (*do_chunk)(void *ctx, size_t run, uint32_t *residuals, size_t skip, size_t n);
  void (*do_finalize)(void *ctx, size_t run);
  size_t run;
} ChannelRowProcessor;

static void channel_row_processor_process_chunk(ChannelRowProcessor *proc,
                                                const int32_t *row, const int32_t *row_left,
                                                const int32_t *row_top, const int32_t *row_topleft,
                                                size_t n) {
  JXL_SL_ALIGN64 uint32_t residuals[8];
  size_t prefix_size = 0;
  size_t required_prefix_size = 0;
  size_t ix;
  /* Only touch the first n samples; CHUNK_SIZE is the max chunk width. */
  for (ix = 0; ix < n; ix++) {
    int32_t px = row[ix];
    int32_t left = row_left[ix];
    int32_t top = row_top[ix];
    int32_t topleft = row_topleft[ix];
    int32_t ac = left - topleft;
    int32_t ab = left - top;
    int32_t bc = top - topleft;
    int32_t grad = (int32_t)((uint32_t)ac + (uint32_t)top);
    int32_t d = ab ^ bc;
    int32_t clamp = d < 0 ? top : left;
    int32_t s = ac ^ bc;
    int32_t pred = s < 0 ? grad : clamp;
    residuals[ix] = jxl_pack_signed(px - pred);
    prefix_size = prefix_size == required_prefix_size
                      ? prefix_size + (residuals[ix] == 0)
                      : prefix_size;
    required_prefix_size += 1;
  }
  if (prefix_size == n && (proc->run > 0 || prefix_size > JXL_SL_LZ77_MIN_LENGTH)) {
    proc->run += prefix_size;
  } else if (prefix_size + proc->run > JXL_SL_LZ77_MIN_LENGTH) {
    proc->do_chunk(proc->ctx, proc->run + prefix_size, residuals, prefix_size, n);
    proc->run = 0;
  } else {
    proc->do_chunk(proc->ctx, 0, residuals, 0, n);
  }
}

static void channel_row_processor_process_row(ChannelRowProcessor *proc,
                                              const int32_t *row, const int32_t *row_left,
                                              const int32_t *row_top, const int32_t *row_topleft,
                                              size_t xs) {
  size_t x;
  for (x = 0; x < xs; x += JXL_SL_CHUNK_SIZE) {
    channel_row_processor_process_chunk(proc, row + x, row_left + x, row_top + x,
                                        row_topleft + x, JXL_SL_MIN(JXL_SL_CHUNK_SIZE, xs - x));
  }
}

static void channel_row_processor_finalize(ChannelRowProcessor *proc) {
  proc->do_finalize(proc->ctx, proc->run);
}

static uint16_t load_le16(const unsigned char *ptr) {
  return (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
}

static uint16_t swap_endian(uint16_t in) { return (uint16_t)((in >> 8) | (in << 8)); }

static void store_ycocg(int32_t r, int32_t g, int32_t b, int32_t *y, int32_t *co, int32_t *cg) {
  *co = r - b;
  int32_t tmp = b + (*co >> 1);
  *cg = g - tmp;
  *y = tmp + (*cg >> 1);
}

static void fill_row_g8(const unsigned char *rgba, size_t oxs, int32_t *luma) {
  size_t x;
  for (x = 0; x < oxs; x++) luma[x] = rgba[x];
}

static void fill_row_g16(const unsigned char *rgba, size_t oxs, int32_t *luma, int big_endian) {
  size_t x;
  for (x = 0; x < oxs; x++) {
    uint16_t val = load_le16(rgba + 2 * x);
    if (big_endian) val = swap_endian(val);
    luma[x] = val;
  }
}

static void fill_row_ga8(const unsigned char *rgba, size_t oxs, int32_t *luma, int32_t *alpha) {
  size_t x;
  for (x = 0; x < oxs; x++) {
    luma[x] = rgba[2 * x];
    alpha[x] = rgba[2 * x + 1];
  }
}

static void fill_row_ga16(const unsigned char *rgba, size_t oxs, int32_t *luma, int32_t *alpha,
                          int big_endian) {
  size_t x;
  for (x = 0; x < oxs; x++) {
    uint16_t l = load_le16(rgba + 4 * x);
    uint16_t a = load_le16(rgba + 4 * x + 2);
    if (big_endian) { l = swap_endian(l); a = swap_endian(a); }
    luma[x] = l;
    alpha[x] = a;
  }
}

static void fill_row_rgb8(const unsigned char *rgba, size_t oxs, int32_t *y, int32_t *co, int32_t *cg) {
  size_t x;
  for (x = 0; x < oxs; x++) {
    store_ycocg(rgba[3 * x], rgba[3 * x + 1], rgba[3 * x + 2], y + x, co + x, cg + x);
  }
}

static void fill_row_rgb16(const unsigned char *rgba, size_t oxs, int32_t *y, int32_t *co,
                           int32_t *cg, int big_endian) {
  size_t x;
  for (x = 0; x < oxs; x++) {
    uint16_t r = load_le16(rgba + 6 * x);
    uint16_t g = load_le16(rgba + 6 * x + 2);
    uint16_t b = load_le16(rgba + 6 * x + 4);
    if (big_endian) { r = swap_endian(r); g = swap_endian(g); b = swap_endian(b); }
    store_ycocg(r, g, b, y + x, co + x, cg + x);
  }
}

static void fill_row_rgba8(const unsigned char *rgba, size_t oxs, int32_t *y, int32_t *co,
                           int32_t *cg, int32_t *alpha) {
  size_t x;
  for (x = 0; x < oxs; x++) {
    store_ycocg(rgba[4 * x], rgba[4 * x + 1], rgba[4 * x + 2], y + x, co + x, cg + x);
    alpha[x] = rgba[4 * x + 3];
  }
}

static void fill_row_rgba16(const unsigned char *rgba, size_t oxs, int32_t *y, int32_t *co,
                            int32_t *cg, int32_t *alpha, int big_endian) {
  size_t x;
  for (x = 0; x < oxs; x++) {
    uint16_t r = load_le16(rgba + 8 * x);
    uint16_t g = load_le16(rgba + 8 * x + 2);
    uint16_t b = load_le16(rgba + 8 * x + 4);
    uint16_t a = load_le16(rgba + 8 * x + 6);
    if (big_endian) {
      r = swap_endian(r); g = swap_endian(g); b = swap_endian(b); a = swap_endian(a);
    }
    store_ycocg(r, g, b, y + x, co + x, cg + x);
    alpha[x] = a;
  }
}

static int32_t *align_ptr_i32(int32_t *ptr) {
  uintptr_t offset = (uintptr_t)ptr % JXL_SL_ALIGN_BYTES;
  if (offset) ptr += (int)(offset / sizeof(int32_t));
  return ptr;
}

static void process_image_area(jxl_context *alloc, const unsigned char *rgba, size_t x0, size_t y0, size_t xs,
                               size_t yskip, size_t ys, size_t row_stride,
                               const BitDepthInfo *bd, size_t nb_chans, int big_endian,
                               ChannelRowProcessor *processors) {
  int32_t *group_data[4][2];
  size_t c, y;
  for (c = 0; c < nb_chans; c++) {
    group_data[c][0] = (int32_t *)jxl_calloc(alloc, JXL_SL_ROW_NUM_PX, sizeof(int32_t));
    group_data[c][1] = (int32_t *)jxl_calloc(alloc, JXL_SL_ROW_NUM_PX, sizeof(int32_t));
  }
  for (y = 0; y < ys; y++) {
    const unsigned char *rgba_row =
        rgba + row_stride * (y0 + y) + x0 * nb_chans * bd->input_bytes;
    int32_t *crow[4];
    int32_t *prow[4];
    for (c = 0; c < nb_chans; c++) {
      crow[c] = align_ptr_i32(&group_data[c][y & 1][JXL_SL_ROW_PADDING]);
      prow[c] = align_ptr_i32(&group_data[c][(y - 1) & 1][JXL_SL_ROW_PADDING]);
    }
    if (nb_chans == 1) {
      if (bd->input_bytes == 1) fill_row_g8(rgba_row, xs, crow[0]);
      else fill_row_g16(rgba_row, xs, crow[0], big_endian);
    } else if (nb_chans == 2) {
      if (bd->input_bytes == 1) fill_row_ga8(rgba_row, xs, crow[0], crow[1]);
      else fill_row_ga16(rgba_row, xs, crow[0], crow[1], big_endian);
    } else if (nb_chans == 3) {
      if (bd->input_bytes == 1) fill_row_rgb8(rgba_row, xs, crow[0], crow[1], crow[2]);
      else fill_row_rgb16(rgba_row, xs, crow[0], crow[1], crow[2], big_endian);
    } else {
      if (bd->input_bytes == 1) fill_row_rgba8(rgba_row, xs, crow[0], crow[1], crow[2], crow[3]);
      else fill_row_rgba16(rgba_row, xs, crow[0], crow[1], crow[2], crow[3], big_endian);
    }
    for (c = 0; c < nb_chans; c++) {
      *(crow[c] - 1) = y > 0 ? *(prow[c]) : 0;
      *(prow[c] - 1) = y > 0 ? *(prow[c]) : 0;
    }
    if (y < yskip) continue;
    for (c = 0; c < nb_chans; c++) {
      const int32_t *row = crow[c];
      const int32_t *row_left = crow[c] - 1;
      const int32_t *row_top = y == 0 ? row_left : prow[c];
      const int32_t *row_topleft = y == 0 ? row_left : prow[c] - 1;
      channel_row_processor_process_row(&processors[c], row, row_left, row_top, row_topleft, xs);
    }
  }
  for (c = 0; c < nb_chans; c++) channel_row_processor_finalize(&processors[c]);
  for (c = 0; c < nb_chans; c++) {
    jxl_free(alloc, group_data[c][0]);
    jxl_free(alloc, group_data[c][1]);
  }
}

static void encoder_chunk_wrapper(void *ctx, size_t run, uint32_t *residuals, size_t skip, size_t n) {
  chunk_encoder_chunk((ChunkEncoder *)ctx, run, residuals, skip, n);
}

static void encoder_finalize_wrapper(void *ctx, size_t run) {
  chunk_encoder_finalize((ChunkEncoder *)ctx, run);
}

static void collector_chunk_wrapper(void *ctx, size_t run, uint32_t *residuals, size_t skip, size_t n) {
  sample_collector_chunk((ChunkSampleCollector *)ctx, run, residuals, skip, n);
}

static void collector_finalize_wrapper(void *ctx, size_t run) {
  sample_collector_finalize((ChunkSampleCollector *)ctx, run);
}



void prepare_dc_global_common(jxl_context *alloc, int is_single_group, size_t width, size_t height,
                              const PrefixCode code[4], BitWriter *output) {
  static const int tree_vals[] = {1, 2, 1, 4, 1, 0, 0, 5, 0, 0, 0, 0, 5,
                                  0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 0, 0};
  size_t i;
  bit_writer_allocate(alloc, output, 100000 + (is_single_group ? width * height * 16 : 0));
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 1, 0);
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 4, 0);
  bit_writer_write(output, 6, 35);
  bit_writer_write(output, 2, 1);
  bit_writer_write(output, 2, 3);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 2, 1);
  bit_writer_write(output, 2, 2);
  bit_writer_write(output, 2, 3);
  bit_writer_write(output, 1, 0);
  {
    uint8_t symbol_bits[6] = {0, 2, 1, 5, 3, 7};
    uint8_t symbol_nbits[6] = {2, 2, 3, 3, 4, 4};
    for (i = 0; i < sizeof(tree_vals) / sizeof(tree_vals[0]); i++) {
      int v = tree_vals[i];
      bit_writer_write(output, symbol_nbits[v], symbol_bits[v]);
    }
  }
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 4, 10);
  bit_writer_write(output, 4, 4);
  bit_writer_write(output, 3, 0);
  bit_writer_write(output, 3, 0);
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 2, 3);
  bit_writer_write(output, 3, 4);
  bit_writer_write(output, 3, 3);
  bit_writer_write(output, 3, 2);
  bit_writer_write(output, 3, 1);
  bit_writer_write(output, 3, 0);
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 4, 0);
  for (i = 0; i < 4; i++) bit_writer_write(output, 4, 0);
  bit_writer_write(output, 5, 1);
  for (i = 0; i < 4; i++) {
    bit_writer_write(output, 1, 1);
    bit_writer_write(output, 4, 8);
    bit_writer_write(output, 8, 256);
  }
  bit_writer_write(output, 2, 1);
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 1, 1);
  for (i = 0; i < 4; i++) prefix_code_write_to(alloc, &code[i], output);
  bit_writer_write(output, 1, 1);
  bit_writer_write(output, 1, 1);
}

void prepare_dc_global(jxl_context *alloc, int is_single_group, size_t width, size_t height, size_t nb_chans,
                       const PrefixCode code[4], BitWriter *output) {
  prepare_dc_global_common(alloc, is_single_group, width, height, code, output);
  if (nb_chans > 2) {
    bit_writer_write(output, 2, 1);
    bit_writer_write(output, 2, 0);
    bit_writer_write(output, 5, 0);
    bit_writer_write(output, 2, 0);
  } else {
    bit_writer_write(output, 2, 0);
  }
  if (!is_single_group) bit_writer_zero_pad_to_byte(output);
}

static void write_ac_section(jxl_context *alloc, const unsigned char *rgba, size_t x0, size_t y0, size_t xs,
                             size_t ys, size_t row_stride, int is_single_group,
                             const BitDepthInfo *bd, size_t nb_chans, int big_endian,
                             const PrefixCode code[4], BitWriterGroup *output) {
  size_t c;
  ChunkEncoder encoders[4];
  ChannelRowProcessor row_encoders[4];
  for (c = 0; c < nb_chans; c++) {
    if (is_single_group && c == 0) continue;
    bit_writer_allocate(alloc, &output->writers[c], xs * ys * bd->max_encoded_bits_per_sample + 4);
  }
  if (!is_single_group) {
    bit_writer_write(&output->writers[0], 1, 1);
    bit_writer_write(&output->writers[0], 1, 1);
    bit_writer_write(&output->writers[0], 2, 0);
  }
  for (c = 0; c < nb_chans; c++) {
    encoders[c].output = &output->writers[c];
    encoders[c].code = &code[c];
    row_encoders[c].ctx = &encoders[c];
    row_encoders[c].do_chunk = encoder_chunk_wrapper;
    row_encoders[c].do_finalize = encoder_finalize_wrapper;
    row_encoders[c].run = 0;
  }
  process_image_area(alloc, rgba, x0, y0, xs, 0, ys, row_stride, bd, nb_chans, big_endian, row_encoders);
}

static uint32_t pixel_hash(uint32_t p) {
  return (p * JXL_SL_HASH_MULTIPLIER) >> (32 - JXL_SL_HASH_EXP);
}

static void fill_row_palette(const unsigned char *inrow, size_t xs, size_t nb_chans,
                             const int16_t *lookup, int16_t *out) {
  size_t x, i;
  for (x = 0; x < xs; x++) {
    uint32_t p = 0;
    for (i = 0; i < nb_chans; ++i) p |= (uint32_t)inrow[x * nb_chans + i] << (8 * i);
    out[x] = lookup[pixel_hash(p)];
  }
}

static void process_image_area_palette(jxl_context *alloc, const unsigned char *rgba, size_t x0, size_t y0,
                                       size_t xs, size_t yskip, size_t ys, size_t row_stride,
                                       const int16_t *lookup, size_t nb_chans,
                                       ChannelRowProcessor *row_encoder) {
  int16_t *group_data[2];
  size_t y;
  group_data[0] = (int16_t *)jxl_calloc(alloc, 256 + JXL_SL_ROW_PADDING * 2, sizeof(int16_t));
  group_data[1] = (int16_t *)jxl_calloc(alloc, 256 + JXL_SL_ROW_PADDING * 2, sizeof(int16_t));
  for (y = 0; y < ys; y++) {
    const unsigned char *inrow = rgba + row_stride * (y0 + y) + x0 * nb_chans;
    int16_t *outrow = &group_data[y & 1][JXL_SL_ROW_PADDING];
    fill_row_palette(inrow, xs, nb_chans, lookup, outrow);
    group_data[y & 1][JXL_SL_ROW_PADDING - 1] = y > 0 ? group_data[(y - 1) & 1][JXL_SL_ROW_PADDING] : 0;
    group_data[(y - 1) & 1][JXL_SL_ROW_PADDING - 1] = y > 0 ? group_data[(y - 1) & 1][JXL_SL_ROW_PADDING] : 0;
    if (y < yskip) continue;
    {
      JXL_SL_ALIGN64 int32_t row32[256];
      JXL_SL_ALIGN64 int32_t left32[256];
      JXL_SL_ALIGN64 int32_t top32[256];
      JXL_SL_ALIGN64 int32_t topleft32[256];
      size_t xi;
      for (xi = 0; xi < xs; xi++) row32[xi] = group_data[y & 1][JXL_SL_ROW_PADDING + xi];
      for (xi = 0; xi < xs; xi++) left32[xi] = group_data[y & 1][JXL_SL_ROW_PADDING - 1 + xi];
      for (xi = 0; xi < xs; xi++)
        top32[xi] = y == 0 ? left32[xi] : group_data[(y - 1) & 1][JXL_SL_ROW_PADDING + xi];
      for (xi = 0; xi < xs; xi++)
        topleft32[xi] = y == 0 ? left32[xi] : group_data[(y - 1) & 1][JXL_SL_ROW_PADDING - 1 + xi];
      channel_row_processor_process_row(row_encoder, row32, left32, top32, topleft32, xs);
    }
  }
  channel_row_processor_finalize(row_encoder);
  jxl_free(alloc, group_data[0]);
  jxl_free(alloc, group_data[1]);
}

static void write_ac_section_palette(jxl_context *alloc, const unsigned char *rgba, size_t x0, size_t y0, size_t xs,
                                     size_t ys, size_t row_stride, int is_single_group,
                                     const PrefixCode code[4], const int16_t *lookup,
                                     size_t nb_chans, BitWriter *output) {
  ChunkEncoder encoder;
  ChannelRowProcessor row_encoder;
  if (!is_single_group) {
    bit_writer_allocate(alloc, output, 16 * xs * ys + 4);
    bit_writer_write(output, 1, 1);
    bit_writer_write(output, 1, 1);
    bit_writer_write(output, 2, 0);
  }
  encoder.output = output;
  encoder.code = &code[is_single_group ? 1 : 0];
  row_encoder.ctx = &encoder;
  row_encoder.do_chunk = encoder_chunk_wrapper;
  row_encoder.do_finalize = encoder_finalize_wrapper;
  row_encoder.run = 0;
  process_image_area_palette(alloc, rgba, x0, y0, xs, 0, ys, row_stride, lookup, nb_chans, &row_encoder);
}

static void collect_samples(jxl_context *alloc, const unsigned char *rgba, size_t x0, size_t y0, size_t xs,
                            size_t row_stride, size_t row_count,
                            uint64_t raw_counts[4][19], uint64_t lz77_counts[4][33],
                            int is_single_group, int palette, const BitDepthInfo *bd,
                            size_t nb_chans, int big_endian, const int16_t *lookup) {
  size_t c;
  if (palette) {
    ChunkSampleCollector sample_collectors[4];
    ChannelRowProcessor row_sample_collectors[4];
    for (c = 0; c < nb_chans; c++) {
      sample_collectors[c].raw_counts = raw_counts[is_single_group ? 1 : 0];
      sample_collectors[c].lz77_counts = lz77_counts[is_single_group ? 1 : 0];
      row_sample_collectors[c].ctx = &sample_collectors[c];
      row_sample_collectors[c].do_chunk = collector_chunk_wrapper;
      row_sample_collectors[c].do_finalize = collector_finalize_wrapper;
      row_sample_collectors[c].run = 0;
    }
    process_image_area_palette(alloc, rgba, x0, y0, xs, 1, 1 + row_count, row_stride, lookup, nb_chans,
                               &row_sample_collectors[0]);
  } else {
    ChunkSampleCollector sample_collectors[4];
    ChannelRowProcessor row_sample_collectors[4];
    for (c = 0; c < nb_chans; c++) {
      sample_collectors[c].raw_counts = raw_counts[c];
      sample_collectors[c].lz77_counts = lz77_counts[c];
      row_sample_collectors[c].ctx = &sample_collectors[c];
      row_sample_collectors[c].do_chunk = collector_chunk_wrapper;
      row_sample_collectors[c].do_finalize = collector_finalize_wrapper;
      row_sample_collectors[c].run = 0;
    }
    process_image_area(alloc, rgba, x0, y0, xs, 1, 1 + row_count, row_stride, bd, nb_chans, big_endian,
                       row_sample_collectors);
  }
}

void prepare_dc_global_palette(jxl_context *alloc, int is_single_group, size_t width, size_t height, size_t nb_chans,
                               const PrefixCode code[4], const uint32_t *palette, size_t pcolors,
                               BitWriter *output) {
  ChunkEncoder encoder;
  ChannelRowProcessor row_encoder;
  int32_t p[4][32 + 1024];
  size_t i;
  prepare_dc_global_common(alloc, is_single_group, width, height, code, output);
  bit_writer_write(output, 2, 1);
  bit_writer_write(output, 2, 1);
  bit_writer_write(output, 5, 0);
  if (nb_chans == 1) bit_writer_write(output, 2, 0);
  else if (nb_chans == 3) bit_writer_write(output, 2, 1);
  else if (nb_chans == 4) bit_writer_write(output, 2, 2);
  else { bit_writer_write(output, 2, 3); bit_writer_write(output, 13, nb_chans - 1); }
  if (pcolors < 256) { bit_writer_write(output, 2, 0); bit_writer_write(output, 8, pcolors); }
  else { bit_writer_write(output, 2, 1); bit_writer_write(output, 10, pcolors - 256); }
  bit_writer_write(output, 2, 0);
  bit_writer_write(output, 4, 0);
  encoder.output = output;
  encoder.code = &code[0];
  row_encoder.ctx = &encoder;
  row_encoder.do_chunk = encoder_chunk_wrapper;
  row_encoder.do_finalize = encoder_finalize_wrapper;
  row_encoder.run = 0;
  /*
   * Lookup index 0 is reserved (pcolors starts at 1 when the palette is built),
   * so the encoded palette row is [0, color0, color1, ...] of length pcolors.
   * Zero the scratch so the leading entry and CHUNK_SIZE padding are defined.
   */
  memset(p, 0, sizeof(p));
  for (i = 0; i + 1 < pcolors; i++) {
    p[0][16 + i + 1] = (int32_t)(palette[i] & 0xFF);
    p[1][16 + i + 1] = (int32_t)((palette[i] >> 8) & 0xFF);
    p[2][16 + i + 1] = (int32_t)((palette[i] >> 16) & 0xFF);
    p[3][16 + i + 1] = (int32_t)((palette[i] >> 24) & 0xFF);
  }
  p[0][15] = 0;
  channel_row_processor_process_row(&row_encoder, p[0] + 16, p[0] + 15, p[0] + 15, p[0] + 15, pcolors);
  p[1][15] = p[0][16];
  p[0][15] = p[0][16];
  if (nb_chans > 1)
    channel_row_processor_process_row(&row_encoder, p[1] + 16, p[1] + 15, p[0] + 16, p[0] + 15, pcolors);
  p[2][15] = p[1][16];
  p[1][15] = p[1][16];
  if (nb_chans > 2)
    channel_row_processor_process_row(&row_encoder, p[2] + 16, p[2] + 15, p[1] + 16, p[1] + 15, pcolors);
  p[3][15] = p[2][16];
  p[2][15] = p[2][16];
  if (nb_chans > 3)
    channel_row_processor_process_row(&row_encoder, p[3] + 16, p[3] + 15, p[2] + 16, p[2] + 15, pcolors);
  channel_row_processor_finalize(&row_encoder);
  if (!is_single_group) bit_writer_zero_pad_to_byte(output);
}

static int detect_palette(const unsigned char *r, size_t width, size_t nb_chans,
                           uint32_t *palette, int *collided_out) {
  size_t x, i;
  int collided = *collided_out;
  for (x = 0; x < width; x++) {
    uint32_t p = 0;
    for (i = 0; i < nb_chans; ++i) p |= (uint32_t)r[x * nb_chans + i] << (8 * i);
    uint32_t index = pixel_hash(p);
    collided |= (palette[index] != 0 && p != palette[index]);
    palette[index] = p;
  }
  *collided_out = collided;
  return collided;
}

typedef struct PaletteSortCtx {
  size_t nb_chans;
} PaletteSortCtx;

static int palette_compare(const void *ap, const void *bp, void *arg) {
  const PaletteSortCtx *ctx = (const PaletteSortCtx *)arg;
  uint32_t a = *(const uint32_t *)ap;
  uint32_t b = *(const uint32_t *)bp;
  uint8_t ac[4], bc[4];
  int i;
  float ay, by;
  if (a == 0) return 1;
  if (b == 0) return -1;
  for (i = 0; i < 4; ++i) {
    ac[i] = (uint8_t)((a >> (8 * i)) & 0xFF);
    bc[i] = (uint8_t)((b >> (8 * i)) & 0xFF);
  }
  if (ctx->nb_chans == 4) {
    ay = (0.299f * ac[0] + 0.587f * ac[1] + 0.114f * ac[2] + 0.01f) * ac[3];
    by = (0.299f * bc[0] + 0.587f * bc[1] + 0.114f * bc[2] + 0.01f) * bc[3];
  } else {
    ay = (0.299f * ac[0] + 0.587f * ac[1] + 0.114f * ac[2] + 0.01f);
    by = (0.299f * bc[0] + 0.587f * bc[1] + 0.114f * bc[2] + 0.01f);
  }
  return ay < by ? -1 : ay > by ? 1 : 0;
}

#if 0 && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 8))
#define HAVE_QSORT_R 1
#endif

#ifdef HAVE_QSORT_R
static int palette_compare_r(const void *ap, const void *bp, void *arg) {
  return palette_compare(ap, bp, arg);
}
#else
static PaletteSortCtx g_palette_sort_ctx;
static int palette_compare_thunk(const void *ap, const void *bp) {
  return palette_compare(ap, bp, &g_palette_sort_ctx);
}
#endif

typedef struct SampleRowsCtx {
  jxl_context *alloc;
  JxlChunkedFrameInputSource input;
  size_t width;
  size_t height;
  size_t bitdepth;
  size_t nb_chans;
  int big_endian;
  int collided;
  int16_t *lookup;
  size_t num_groups;
  uint64_t (*raw_counts)[19];
  uint64_t (*lz77_counts)[33];
} SampleRowsCtx;

static void sample_rows_impl(void *opaque, size_t xg, size_t yg, size_t num_rows) {
  SampleRowsCtx *ctx = (SampleRowsCtx *)opaque;
  size_t y0 = yg * 256;
  size_t x0 = xg * 256;
  size_t ys = JXL_SL_MIN(ctx->height - y0, (size_t)256);
  size_t xs = JXL_SL_MIN(ctx->width - x0, (size_t)256);
  size_t stride;
  const void *buffer =
      ctx->input.get_color_channel_data_at(ctx->input.opaque, x0, y0, xs, ys, &stride);
  const unsigned char *rgba = (const unsigned char *)buffer;
  ssize_t y_begin_group = JXL_SL_MAX((ssize_t)0, (ssize_t)ys - (ssize_t)num_rows) / 2;
  int y_count = (int)JXL_SL_MIN(num_rows, ys - (size_t)y_begin_group);
  int x_max = (int)(xs / JXL_SL_CHUNK_SIZE * JXL_SL_CHUNK_SIZE);
  BitDepthInfo bd = get_bitdepth_info(ctx->bitdepth);
  int onegroup = ctx->num_groups == 1;
  collect_samples(ctx->alloc, rgba, 0, (size_t)y_begin_group, (size_t)x_max, stride, (size_t)y_count,
                  ctx->raw_counts, ctx->lz77_counts, onegroup, !ctx->collided, &bd, ctx->nb_chans,
                  ctx->big_endian != 0, ctx->lookup);
  ctx->input.release_buffer(ctx->input.opaque, buffer);
}

static JxlSimpleLosslessFrameState *ll_prepare(jxl_context *alloc, JxlChunkedFrameInputSource input, size_t width,
                                               size_t height, const BitDepthInfo *bd,
                                               size_t nb_chans, int big_endian, int effort,
                                               int oneshot) {
  assert(width != 0);
  assert(height != 0);
  uint32_t palette[JXL_SL_HASH_SIZE];
  int16_t *lookup;
  int pcolors = 0;
  int collided;
  size_t y0, x0;
  memset(palette, 0, sizeof(palette));
  lookup = (int16_t *)jxl_calloc(alloc, JXL_SL_HASH_SIZE, sizeof(int16_t));
  lookup[0] = 0;
  collided = effort < 2 || bd->bitdepth != 8 || !oneshot;
  for (y0 = 0; y0 < height && !collided; y0 += 256) {
    size_t ys = JXL_SL_MIN(height - y0, (size_t)256);
    for (x0 = 0; x0 < width && !collided; x0 += 256) {
      size_t xs = JXL_SL_MIN(width - x0, (size_t)256);
      size_t stride;
      const void *buffer = input.get_color_channel_data_at(input.opaque, x0, y0, xs, ys, &stride);
      const unsigned char *rgba = (const unsigned char *)buffer;
      size_t y;
      for (y = 0; y < ys && !collided; y++) {
        detect_palette(rgba + stride * y, xs, nb_chans, palette, &collided);
      }
      input.release_buffer(input.opaque, buffer);
    }
  }
  {
    int nb_entries = 0;
    if (!collided) {
      pcolors = 1;
      int have_color = 0;
      uint8_t minG = 255, maxG = 0;
      uint32_t k;
      for (k = 0; k < JXL_SL_HASH_SIZE; k++) {
        if (palette[k] == 0) continue;
        uint8_t p[4];
        int i;
        for (i = 0; i < 4; ++i) p[i] = (uint8_t)((palette[k] >> (8 * i)) & 0xFF);
        palette[nb_entries] = palette[k];
        if (p[0] != p[1] || p[0] != p[2]) have_color = 1;
        if (p[1] < minG) minG = p[1];
        if (p[1] > maxG) maxG = p[1];
        nb_entries++;
        if (nb_entries + pcolors > JXL_SL_MAX_COLORS) { collided = 1; break; }
      }
      if (!have_color && maxG - minG < nb_entries * 1.4f) collided = 1;
      if (!collided) {
        PaletteSortCtx sort_ctx = {nb_chans};
#ifdef HAVE_QSORT_R
        qsort_r(palette, (size_t)nb_entries, sizeof(uint32_t), palette_compare_r, &sort_ctx);
#else
        g_palette_sort_ctx = sort_ctx;
        qsort(palette, (size_t)nb_entries, sizeof(uint32_t), palette_compare_thunk);
#endif
        for (k = 0; k < (uint32_t)nb_entries; k++) {
          if (palette[k] == 0) break;
          lookup[pixel_hash(palette[k])] = (int16_t)pcolors++;
        }
      }
    }
  }
  {
    size_t num_groups_x = (width + 255) / 256;
    size_t num_groups_y = (height + 255) / 256;
    size_t num_dc_groups_x = (width + 2047) / 2048;
    size_t num_dc_groups_y = (height + 2047) / 2048;
    uint64_t raw_counts[4][19];
    uint64_t lz77_counts[4][33];
    size_t c, i;
    int onegroup = num_groups_x == 1 && num_groups_y == 1;
    SampleRowsCtx sctx;
    memset(raw_counts, 0, sizeof(raw_counts));
    memset(lz77_counts, 0, sizeof(lz77_counts));
    sctx.alloc = alloc;
    sctx.input = input;
    sctx.width = width;
    sctx.height = height;
    sctx.bitdepth = bd->bitdepth;
    sctx.nb_chans = nb_chans;
    sctx.big_endian = big_endian ? 1 : 0;
    sctx.collided = collided;
    sctx.lookup = lookup;
    sctx.num_groups = onegroup ? 1 : (2 + num_dc_groups_x * num_dc_groups_y + num_groups_x * num_groups_y);
    sctx.raw_counts = raw_counts;
    sctx.lz77_counts = lz77_counts;
    if (oneshot || effort >= 64) {
      size_t g, num = num_groups_y * num_groups_x;
      for (g = 0; g < num; g++) {
        size_t xg = g % num_groups_x;
        size_t yg = g / num_groups_x;
        size_t y0g = yg * 256;
        size_t ys = JXL_SL_MIN(height - y0g, (size_t)256);
        sample_rows_impl(&sctx, xg, yg, 2 * (size_t)effort * ys / 256);
      }
    } else {
      sample_rows_impl(&sctx, (num_groups_x - 1) / 2, (num_groups_y - 1) / 2,
                       2 * (size_t)effort * num_groups_x * num_groups_y);
    }
    {
      uint64_t base_raw_counts[19] = {
          3843, 852, 1270, 1214, 1014, 727, 481, 300, 159, 51,
          5, 1, 1, 1, 1, 1, 1, 1, 1};
      int doing_ycocg = nb_chans > 2 && collided;
      int large_palette = !collided || pcolors >= 256;
      size_t sym_start = bitdepth_num_symbols(bd, doing_ycocg || large_palette);
      for (i = sym_start; i < 19; i++) base_raw_counts[i] = 0;
      for (c = 0; c < 4; c++)
        for (i = 0; i < 19; i++) raw_counts[c][i] = (raw_counts[c][i] << 8) + base_raw_counts[i];
      if (!collided) {
        unsigned token, nbits, bits;
        encode_hybrid_uint000(jxl_pack_signed(pcolors - 1), &token, &nbits, &bits);
        for (i = 0; i < token + 1; i++)
          raw_counts[0][i] = JXL_SL_MAX(raw_counts[0][i], (uint64_t)1);
        for (i = token + 1; i < 10; i++) raw_counts[0][i] = 1;
        (void)nbits;
        (void)bits;
      }
    }
    {
      uint64_t base_lz77_counts[33] = {
          29, 27, 25, 23, 21, 21, 19, 18, 21, 17, 16, 15, 15, 14,
          13, 13, 137, 98, 61, 34, 1, 1, 1, 1, 1, 1, 1, 1,
      };
      for (c = 0; c < 4; c++)
        for (i = 0; i < 33; i++) lz77_counts[c][i] = (lz77_counts[c][i] << 8) + base_lz77_counts[i];
    }
    {
      JxlSimpleLosslessFrameState *frame_state =
          (JxlSimpleLosslessFrameState *)jxl_calloc(alloc, 1, sizeof(JxlSimpleLosslessFrameState));
      size_t num_dc_groups = num_dc_groups_x * num_dc_groups_y;
      size_t num_ac_groups = num_groups_x * num_groups_y;
      size_t num_groups = onegroup ? 1 : (2 + num_dc_groups + num_ac_groups);
      for (i = 0; i < 4; i++)
        prefix_code_init(alloc, &frame_state->hcode[i], bd, raw_counts[i], lz77_counts[i]);
      frame_state->alloc = alloc;
  frame_state->input = input;
      frame_state->width = width;
      frame_state->height = height;
      frame_state->num_groups_x = num_groups_x;
      frame_state->num_groups_y = num_groups_y;
      frame_state->num_dc_groups_x = num_dc_groups_x;
      frame_state->num_dc_groups_y = num_dc_groups_y;
      frame_state->nb_chans = nb_chans;
      frame_state->bitdepth = bd->bitdepth;
      frame_state->big_endian = big_endian ? 1 : 0;
      frame_state->effort = effort;
      frame_state->collided = collided;
      frame_state->lookup = lookup;
      frame_state->num_groups = num_groups;
      frame_state->group_data = (BitWriterGroup *)jxl_calloc(alloc, num_groups, sizeof(BitWriterGroup));
      frame_state->group_sizes = (size_t *)jxl_calloc(alloc, num_groups, sizeof(size_t));
      if (collided) {
        prepare_dc_global(alloc, onegroup, width, height, nb_chans, frame_state->hcode,
                          &frame_state->group_data[0].writers[0]);
      } else {
        prepare_dc_global_palette(alloc, onegroup, width, height, nb_chans, frame_state->hcode, palette,
                                  (size_t)pcolors, &frame_state->group_data[0].writers[0]);
      }
      frame_state->group_sizes[0] = section_size(&frame_state->group_data[0]);
      if (!onegroup) {
        compute_ac_group_data_offset(frame_state->group_sizes[0], num_dc_groups, num_ac_groups,
                                     &frame_state->min_dc_global_size,
                                     &frame_state->ac_group_data_offset);
      }
      return frame_state;
    }
  }
}

typedef struct ProcessGroupCtx {
  JxlSimpleLosslessFrameState *frame_state;
  const BitDepthInfo *bd;
} ProcessGroupCtx;

static void process_one_group(void *opaque, size_t g) {
  ProcessGroupCtx *pctx = (ProcessGroupCtx *)opaque;
  JxlSimpleLosslessFrameState *frame_state = pctx->frame_state;
  const BitDepthInfo *bd = pctx->bd;
  size_t xg = g % frame_state->num_groups_x;
  size_t yg = g / frame_state->num_groups_x;
  size_t num_dc_groups = frame_state->num_dc_groups_x * frame_state->num_dc_groups_y;
  int onegroup = frame_state->num_groups == 1;
  size_t group_id = onegroup ? 0 : (2 + num_dc_groups + g);
  size_t xs = JXL_SL_MIN(frame_state->width - xg * 256, (size_t)256);
  size_t ys = JXL_SL_MIN(frame_state->height - yg * 256, (size_t)256);
  size_t x0 = xg * 256;
  size_t y0 = yg * 256;
  size_t stride;
  JxlChunkedFrameInputSource input = frame_state->input;
  const void *buffer = input.get_color_channel_data_at(input.opaque, x0, y0, xs, ys, &stride);
  const unsigned char *rgba = (const unsigned char *)buffer;
  if (frame_state->collided) {
    write_ac_section(frame_state->alloc, rgba, 0, 0, xs, ys, stride, onegroup, bd, frame_state->nb_chans,
                     frame_state->big_endian != 0, frame_state->hcode,
                     &frame_state->group_data[group_id]);
  } else {
    write_ac_section_palette(frame_state->alloc, rgba, 0, 0, xs, ys, stride, onegroup, frame_state->hcode,
                             frame_state->lookup, frame_state->nb_chans,
                             &frame_state->group_data[group_id].writers[0]);
  }
  frame_state->group_sizes[group_id] = section_size(&frame_state->group_data[group_id]);
  input.release_buffer(input.opaque, buffer);
}

static int ll_process(JxlSimpleLosslessFrameState *frame_state, int is_last,
                       const BitDepthInfo *bd, void *runner_opaque, JxlSlParallelRunner runner) {
  size_t total_groups = frame_state->num_groups_x * frame_state->num_groups_y;
  ProcessGroupCtx pctx = {frame_state, bd};
  (void)is_last;
  runner(runner_opaque, &pctx, process_one_group, total_groups);
  return 1;
}

typedef struct {
  const uint8_t *rgba;
  size_t row_stride;
  size_t bytes_per_pixel;
} jxl_sl_frame_input;

static const void *jxl_sl_frame_get_data_at(void *opaque, size_t xpos, size_t ypos,
                                            size_t xsize, size_t ysize, size_t *row_offset) {
  jxl_sl_frame_input *self = (jxl_sl_frame_input *)opaque;
  (void)xsize;
  (void)ysize;
  *row_offset = self->row_stride;
  return self->rgba + ypos * (*row_offset) + xpos * self->bytes_per_pixel;
}

static void jxl_sl_frame_release_buffer(void *opaque, const void *buf) {
  (void)opaque;
  (void)buf;
}

static JxlChunkedFrameInputSource jxl_sl_frame_input_source(jxl_sl_frame_input *input) {
  JxlChunkedFrameInputSource src;
  src.opaque = input;
  src.get_color_channel_data_at = jxl_sl_frame_get_data_at;
  src.release_buffer = jxl_sl_frame_release_buffer;
  return src;
}

static void jxl_sl_trivial_runner(void *runner_opaque, void *opaque,
                                    void (*fun)(void *, size_t), size_t count) {
  size_t i;
  (void)runner_opaque;
  for (i = 0; i < count; i++) {
    fun(opaque, i);
  }
}

static jxl_status_t jxl_sl_encode_buffer(
    jxl_context *alloc, const uint8_t *rgba, size_t width, size_t row_stride,
    size_t height, size_t nb_chans, size_t bitdepth, int big_endian, int effort,
    uint8_t **output, size_t *output_len) {
  BitDepthInfo bd;
  jxl_sl_frame_input input;
  JxlSimpleLosslessFrameState *frame_state;
  size_t output_size, written, total;
  JxlChunkedFrameInputSource src;

  if (alloc == NULL || rgba == NULL || output == NULL || output_len == NULL) {
    return JXL_ERROR_INVALID_INPUT;
  }
  if (width == 0 || height == 0 || nb_chans == 0 || nb_chans > 4 || bitdepth == 0 ||
      bitdepth > 16) {
    return JXL_ERROR_INVALID_INPUT;
  }

  bd = get_bitdepth_info(bitdepth);
  input.rgba = rgba;
  input.row_stride = row_stride;
  input.bytes_per_pixel = bitdepth <= 8 ? nb_chans : 2 * nb_chans;
  src = jxl_sl_frame_input_source(&input);

  frame_state = ll_prepare(alloc, src, width, height, &bd, nb_chans, big_endian, effort, 1);
  if (frame_state == NULL) {
    return JXL_ERROR_OUT_OF_MEMORY;
  }
  if (!ll_process(frame_state, 1, &bd, NULL, jxl_sl_trivial_runner)) {
    jxl_sl_free_frame_state(frame_state);
    return JXL_ERROR_INVALID_INPUT;
  }
  jxl_sl_prepare_header(frame_state, 1, 1);
  output_size = jxl_sl_max_required_output(frame_state);
  *output = (uint8_t *)jxl_alloc(alloc, output_size);
  if (*output == NULL) {
    jxl_sl_free_frame_state(frame_state);
    return JXL_ERROR_OUT_OF_MEMORY;
  }
  total = 0;
  while ((written = jxl_sl_write_output(frame_state, *output + total, output_size - total)) != 0) {
    total += written;
  }
  jxl_sl_free_frame_state(frame_state);
  *output_len = total;
  return JXL_OK;
}

jxl_status_t jxl_simple_lossless_encode(jxl_context *ctx,
                                        const jxl_simple_lossless_image_desc *desc,
                                        const uint8_t *pixels, size_t row_stride,
                                        uint8_t **jxl_out, size_t *jxl_len) {
  jxl_context *alloc;

  if (ctx == NULL || desc == NULL || pixels == NULL || jxl_out == NULL || jxl_len == NULL) {
    return JXL_ERROR_INVALID_INPUT;
  }
  if (desc->reserved != 0) {
    return JXL_ERROR_INVALID_INPUT;
  }
  alloc = ctx;
  return jxl_sl_encode_buffer(alloc, pixels, desc->width, row_stride, desc->height,
                              desc->num_channels, desc->bits_per_sample, desc->big_endian,
                              desc->effort, jxl_out, jxl_len);
}
