// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include "ans_common.h"

#include <stddef.h>
#include <stdint.h>

#include "ans_params.h"
#include "base/array.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"

// First, all trailing non-occurring symbols are removed from the distribution;
// if this leaves the distribution empty, a placeholder symbol with max weight
// is  added. This ensures that the resulting distribution sums to total table
// size. Then, `entry_size` is chosen to be the largest power of two so that
// `table_size` = ANS_TAB_SIZE/`entry_size` is at least as big as the
// distribution size.
// Note that each entry will only ever contain two different symbols, and
// consecutive ranges of offsets, which allows us to use a compact
// representation.
// Each entry is initialized with only the (symbol=i, offset) pairs; then
// positions for which the entry overflows (i.e. distribution[i] > entry_size)
// or is not full are computed, and put into a stack in increasing order.
// Missing symbols in the distribution are padded with 0 (because `table_size`
// >= number of symbols). The `cutoff` value for each entry is initialized to
// the number of occupied slots in that entry (i.e. `distributions[i]`). While
// the overflowing-symbol stack is not empty (which implies that the
// underflowing-symbol stack also is not), the top overfull and underfull
// positions are popped from the stack; the empty slots in the underfull entry
// are then filled with as many slots as needed from the overfull entry; such
// slots are placed after the slots in the overfull entry, and `offsets[1]` is
// computed accordingly. The formerly underfull entry is thus now neither
// underfull nor overfull, and represents exactly two symbols. The overfull
// entry might be either overfull or underfull, and is pushed into the
// corresponding stack.
jxl_enc_status jxl_init_alias_table_body(const int32_t* counts, size_t counts_size,
                          uint32_t log_range, size_t log_alpha_size,
                          jxl_alias_table_entry* JXL_RESTRICT a,
                          jxl_array_i32* distribution, jxl_array_u32* underfull_posn,
                          jxl_array_u32* overfull_posn, jxl_array_u32* cutoffs) {
  JXL_RETURN_IF_ERROR(jxl_array_init(distribution, distribution->ctx));
  JXL_RETURN_IF_ERROR(jxl_array_append(distribution, counts, counts_size));

  const uint32_t range = 1 << log_range;
  const size_t table_size = 1 << log_alpha_size;
  JXL_ENSURE(table_size <= range);
  while (!jxl_array_empty(distribution) && jxl_array_back(distribution) == 0) {
    jxl_array_pop_back(distribution);
  }
  // Ensure that a valid table is always returned, even for an empty
  // alphabet. Otherwise, a specially-crafted stream might crash the
  // decoder.
  if (jxl_array_empty(distribution)) {
    JXL_RETURN_IF_ERROR(jxl_array_i32_push_back(distribution, (int32_t)(range)));
  }
  JXL_ENSURE(jxl_array_len(distribution) <= table_size);
  const uint32_t entry_size = range >> log_alpha_size;  // this is exact
  int single_symbol = -1;
  int sum = 0;
  // Special case for single-symbol distributions, that ensures that the state
  // does not change when decoding from such a distribution. Note that, since we
  // hardcode offset0 == 0, it is not straightforward (if at all possible) to
  // fix the general case to produce this result.
  for (size_t sym = 0; sym < jxl_array_len(distribution); sym++) {
    int32_t v = *jxl_array_at(distribution, sym);
    sum += v;
    if (v == ANS_TAB_SIZE) {
      JXL_ENSURE(single_symbol == -1);
      single_symbol = sym;
    }
  }
  JXL_ENSURE((uint32_t)(sum) == range);
  if (single_symbol != -1) {
    uint8_t sym = single_symbol;
    JXL_ENSURE(single_symbol == sym);
    for (size_t i = 0; i < table_size; i++) {
      a[i].right_value = sym;
      a[i].cutoff = 0;
      a[i].offsets1 = entry_size * i;
      a[i].freq0 = 0;
      a[i].freq1_xor_freq0 = ANS_TAB_SIZE;
    }
    return jxl_enc_ok_status();
  }

  JXL_RETURN_IF_ERROR(jxl_array_init(underfull_posn, underfull_posn->ctx));
  JXL_RETURN_IF_ERROR(jxl_array_init(overfull_posn, overfull_posn->ctx));
  JXL_RETURN_IF_ERROR(jxl_array_init(cutoffs, cutoffs->ctx));
  JXL_RETURN_IF_ERROR(jxl_array_resize(cutoffs, (size_t)(1) << log_alpha_size));
  // Initialize entries.
  for (size_t i = 0; i < jxl_array_len(distribution); i++) {
    *jxl_array_at(cutoffs, i) = *jxl_array_at(distribution, i);
    if (*jxl_array_at(cutoffs, i) > entry_size) {
      JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(overfull_posn, (uint32_t)(i)));
    } else if (*jxl_array_at(cutoffs, i) < entry_size) {
      JXL_RETURN_IF_ERROR(
          jxl_array_u32_push_back(underfull_posn, (uint32_t)(i)));
    }
  }
  for (uint32_t i = jxl_array_len(distribution); i < table_size; i++) {
    *jxl_array_at(cutoffs, i) = 0;
    JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(underfull_posn, i));
  }
  // Reassign overflow/underflow values.
  while (!jxl_array_empty(overfull_posn)) {
    uint32_t overfull_i = jxl_array_back(overfull_posn);
    jxl_array_pop_back(overfull_posn);
    JXL_ENSURE(!jxl_array_empty(underfull_posn));
    uint32_t underfull_i = jxl_array_back(underfull_posn);
    jxl_array_pop_back(underfull_posn);
    uint32_t underfull_by = entry_size - *jxl_array_at(cutoffs, underfull_i);
    *jxl_array_at(cutoffs, overfull_i) -= underfull_by;
    // overfull positions have their original symbols
    a[underfull_i].right_value = overfull_i;
    a[underfull_i].offsets1 = *jxl_array_at(cutoffs, overfull_i);
    // Slots in the right part of entry underfull_i were taken from the end
    // of the symbols in entry overfull_i.
    if (*jxl_array_at(cutoffs, overfull_i) < entry_size) {
      JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(underfull_posn, overfull_i));
    } else if (*jxl_array_at(cutoffs, overfull_i) > entry_size) {
      JXL_RETURN_IF_ERROR(jxl_array_u32_push_back(overfull_posn, overfull_i));
    }
  }
  for (uint32_t i = 0; i < table_size; i++) {
    // cutoffs[i] is properly initialized but the clang-analyzer doesn't infer
    // it since it is partially initialized across two for-loops.
    // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult)
    if (*jxl_array_at(cutoffs, i) == entry_size) {
      a[i].right_value = i;
      a[i].offsets1 = 0;
      a[i].cutoff = 0;
    } else {
      // Note that, if cutoff is not equal to entry_size,
      // a[i].offsets1 was initialized with (overfull cutoff) -
      // (entry_size - a[i].cutoff). Thus, subtracting
      // a[i].cutoff cannot make it negative.
      a[i].offsets1 -= *jxl_array_at(cutoffs, i);
      a[i].cutoff = *jxl_array_at(cutoffs, i);
    }
    const size_t freq0 = i < jxl_array_len(distribution) ? *jxl_array_at(distribution, i) : 0;
    const size_t i1 = a[i].right_value;
    const size_t freq1 = i1 < jxl_array_len(distribution) ? *jxl_array_at(distribution, i1) : 0;
    a[i].freq0 = (uint16_t)(freq0);
    a[i].freq1_xor_freq0 = (uint16_t)(freq1 ^ freq0);
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_init_alias_table(jxl_context* mm, const int32_t* counts,
                      size_t counts_size, uint32_t log_range,
                      size_t log_alpha_size,
                      jxl_alias_table_entry* JXL_RESTRICT a) {
  jxl_array_i32 distribution;
  jxl_array_construct_empty(&distribution, mm);
  jxl_array_u32 underfull_posn;
  jxl_array_construct_empty(&underfull_posn, mm);
  jxl_array_u32 overfull_posn;
  jxl_array_construct_empty(&overfull_posn, mm);
  jxl_array_u32 cutoffs;
  jxl_array_construct_empty(&cutoffs, mm);
  jxl_enc_status status =
      jxl_init_alias_table_body(counts, counts_size, log_range, log_alpha_size, a,
                         &distribution, &underfull_posn, &overfull_posn,
                         &cutoffs);
  jxl_array_destroy(&distribution);
  jxl_array_destroy(&underfull_posn);
  jxl_array_destroy(&overfull_posn);
  jxl_array_destroy(&cutoffs);
  return status;
}

