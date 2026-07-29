// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#include <jxl/context.h>
#include "enc_allocator.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ac_strategy.h"
#include "base/array.h"
#include "base/common.h"
#include "base/enc_status.h"
#include "coeff_order.h"
#include "enc_ans.h"
#include "enc_coeff_order.h"
#include "lehmer_code.h"
#include "enc_aligned_memory.h"

#include "layer_type.h"

typedef struct jxl_pos_and_count {
  uint32_t pos;
  // Saving index breaks the ties for non-stable sort
  uint64_t count_and_idx;
} jxl_pos_and_count;

static void jxl_pos_and_count_swap_at(jxl_pos_and_count* a, size_t i, size_t j) {
  jxl_pos_and_count tmp = a[i];
  a[i] = a[j];
  a[j] = tmp;
}

static void jxl_pos_and_count_sift_down(jxl_pos_and_count* a, size_t len, size_t hole) {
  for (;;) {
    size_t child = 2 * hole + 1;
    if (child >= len) break;
    size_t right = child + 1;
    if (right < len &&
        a[child].count_and_idx < a[right].count_and_idx) {
      child = right;
    }
    if (!(a[hole].count_and_idx < a[child].count_and_idx)) break;
    jxl_pos_and_count_swap_at(a, hole, child);
    hole = child;
  }
}

static void jxl_pos_and_count_sort(jxl_pos_and_count* begin, jxl_pos_and_count* end) {
  size_t n = (size_t)(end - begin);
  if (n < 2) return;
  for (size_t i = n / 2; i > 0; --i) {
    jxl_pos_and_count_sift_down(begin, n, i - 1);
  }
  while (n > 1) {
    jxl_pos_and_count_swap_at(begin, 0, n - 1);
    --n;
    jxl_pos_and_count_sift_down(begin, n, 0);
  }
}

jxl_used_orders jxl_compute_used_orders(const jxl_speed_tier speed,
                             const jxl_ac_strategy_image* ac_strategy,
                             const jxl_rect* rect){
  // No coefficient reordering in Falcon or faster.
  // Only uses DCT8 = 0, so bitfield = 1.
  if (speed >= kFalcon) return jxl_used_orders_make(1, 1);

  uint32_t ret = 0;
  uint32_t ret_customize = 0;
  size_t xsize_blocks = jxl_rect_x_size(rect);
  size_t ysize_blocks = jxl_rect_y_size(rect);
  // TODO(veluca): precompute when doing DCT.
  for (size_t by = 0; by < ysize_blocks; ++by) {
    jxl_ac_strategy_row acs_row = jxl_ac_strategy_image_const_row_rect(ac_strategy, rect, by);
    for (size_t bx = 0; bx < xsize_blocks; ++bx) {
      int ord = kStrategyOrder[jxl_ac_strategy_raw_strategy(jxl_ac_strategy_row_at(acs_row, bx))];
      // Do not customize coefficient orders for blocks bigger than 32x32.
      ret |= 1u << ord;
      if (ord > 6) {
        continue;
      }
      ret_customize |= 1u << ord;
    }
  }
  // Use default orders for small images.
  if (jxl_ac_strategy_image_x_size(ac_strategy) < 5 && jxl_ac_strategy_image_y_size(ac_strategy) < 5) {
    return jxl_used_orders_make(ret, 0);
  }
  return jxl_used_orders_make(ret, ret_customize);
}

jxl_enc_status jxl_compute_coeff_order_body(jxl_speed_tier speed, const jxl_ac_image* ac_image,
                             const jxl_ac_strategy_image* ac_strategy,
                             const jxl_frame_dimensions* frame_dim,
                             uint32_t* all_used_orders, uint32_t prev_used_acs,
                             uint32_t current_used_acs,
                             uint32_t current_used_orders,
                             coeff_order_t* JXL_RESTRICT order,
                             jxl_array_i64* num_zeros,
                             jxl_array_u32* natural_order_buffer) {
  jxl_context* ctx = jxl_ac_strategy_image_ctx(ac_strategy);
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(num_zeros, kCoeffOrderMaxSize));
  // If compressing at high speed and only using 8x8 DCTs, only consider a
  // subset of blocks.
  double block_fraction = 1.0f;
  // TODO(veluca): figure out why sampling blocks if non-8x8s are used makes
  // encoding significantly less dense.
  if (speed >= kSquirrel && current_used_orders == 1) {
    block_fraction = 0.5f;
  }
  // No need to compute number of zero coefficients if all orders are the
  // default.
  if (current_used_orders != 0) {
    uint64_t threshold =
        (UINT64_MAX >> 32) * block_fraction;
    jxl_xor_shift128_plus rng = jxl_xor_shift128_plus_make(0x94D049BB133111EBull, 0xBF58476D1CE4E5B9ull);

    // Count number of zero coefficients, separately for each DCT band.
    // TODO(veluca): precompute when doing DCT.
    for (size_t group_index = 0; group_index < frame_dim->num_groups;
         group_index++) {
      const size_t gx = group_index % frame_dim->xsize_groups;
      const size_t gy = group_index / frame_dim->xsize_groups;
      const jxl_rect rect = jxl_rect_make_clamped(
          gx * kGroupDimInBlocks, gy * kGroupDimInBlocks, kGroupDimInBlocks,
          kGroupDimInBlocks, frame_dim->xsize_blocks, frame_dim->ysize_blocks);
      jxl_const_ac_ptr rows[3];
      for (size_t c = 0; c < 3; c++) {
        rows[c] = jxl_ac_image_plane_row_const(ac_image, c, group_index, 0);
      }
      size_t ac_offset = 0;

      // TODO(veluca): SIMDfy.
      for (size_t by = 0; by < jxl_rect_y_size(&rect); ++by) {
        jxl_ac_strategy_row acs_row = jxl_ac_strategy_image_const_row_rect(ac_strategy, &rect, by);
        for (size_t bx = 0; bx < jxl_rect_x_size(&rect); ++bx) {
          jxl_ac_strategy acs = jxl_ac_strategy_row_at(acs_row, bx);
          if (!jxl_ac_strategy_is_first_block(acs)) continue;
          if (!jxl_xor_shift128_plus_below_threshold(&rng, threshold)) continue;
          size_t size = kDCTBlockSize << jxl_ac_strategy_log2_covered_blocks(acs);
          for (size_t c = 0; c < 3; ++c) {
            const size_t order_offset =
                jxl_coeff_order_offset(kStrategyOrder[jxl_ac_strategy_raw_strategy(acs)], c);
            for (size_t k = 0; k < size; k++) {
              bool is_zero = rows[c].ptr32[ac_offset + k] == 0;
              *jxl_array_at(num_zeros, order_offset + k) += is_zero ? 1 : 0;
            }
            // Ensure LLFs are first in the order.
            size_t cx = jxl_ac_strategy_covered_blocks_x(acs);
            size_t cy = jxl_ac_strategy_covered_blocks_y(acs);
            jxl_coefficient_layout(&cy, &cx);
            for (size_t iy = 0; iy < cy; iy++) {
              for (size_t ix = 0; ix < cx; ix++) {
                *jxl_array_at(num_zeros, order_offset + iy * kBlockDim * cx + ix) = -1;
              }
            }
          }
          ac_offset += size;
        }
      }
    }
  }

  size_t mem_bytes = kAcStrategyMaxCoeffArea * sizeof(jxl_pos_and_count);
  jxl_aligned_memory mem;
  jxl_aligned_memory_construct_empty(&mem);
  jxl_enc_status status = jxl_aligned_memory_create(ctx, mem_bytes, 0, &mem);
  if (!jxl_enc_status_ok(status)) {
    jxl_aligned_memory_destroy(&mem);
    return status;
  }

  uint16_t computed = 0;
  for (uint8_t o = 0; o < kAcStrategyNumValidStrategies; ++o) {
    uint8_t ord = kStrategyOrder[o];
    if (computed & (1 << ord)) continue;
    computed |= 1 << ord;
    jxl_ac_strategy acs = jxl_ac_strategy_from_raw_strategy_u8(o);
    size_t sz = kDCTBlockSize * jxl_ac_strategy_covered_blocks_x(acs) * jxl_ac_strategy_covered_blocks_y(acs);
    // Expected maximal size is 256 x 256.
    JXL_DASSERT(sz <= (1 << 16));

    // Do nothing for transforms that don't appear.
    if ((1 << ord) & ~current_used_acs) continue;

    // Do nothing if we already committed to this custom order previously.
    if ((1 << ord) & prev_used_acs) continue;
    if ((1 << ord) & *all_used_orders) continue;

    if (jxl_array_len(natural_order_buffer) < sz) {
      status = jxl_array_resize_zero(natural_order_buffer, sz);
      if (!jxl_enc_status_ok(status)) {
        jxl_aligned_memory_destroy(&mem);
        return status;
      }
    }
    jxl_ac_strategy_compute_natural_coeff_order(acs, jxl_array_data(natural_order_buffer));

    // Ensure natural coefficient order is not permuted if the order is
    // not transmitted.
    if ((1 << ord) & ~current_used_orders) {
      for (size_t c = 0; c < 3; c++) {
        size_t offset = jxl_coeff_order_offset(ord, c);
        JXL_ENSURE(jxl_coeff_order_offset(ord, c + 1) - offset == sz);
        memcpy(&order[offset], jxl_array_data(natural_order_buffer),
               sz * sizeof(*order));
      }
      continue;
    }

    bool is_nondefault = false;
    for (uint8_t c = 0; c < 3; c++) {
      // Apply zig-zag order.
      jxl_pos_and_count* pos_and_val = (jxl_pos_and_count*)(jxl_aligned_memory_address(&mem));
      size_t offset = jxl_coeff_order_offset(ord, c);
      JXL_ENSURE(jxl_coeff_order_offset(ord, c + 1) - offset == sz);
      float inv_sqrt_sz = 1.0f / sqrt(sz);
      for (size_t i = 0; i < sz; ++i) {
        size_t pos = *jxl_array_at(natural_order_buffer, i);
        pos_and_val[i].pos = pos;
        // We don't care for the exact number -> quantize number of zeros,
        // to get less permuted order.
        uint64_t count = *jxl_array_at(num_zeros, offset + pos) * inv_sqrt_sz + 0.1f;
        // Worst case: all dct8x8, all zeroes: count <= nb_pixels/64/8
        // nb_pixels is limited to 2^40 (Level 10 limit)
        // so count is limited to 2^31
        JXL_DASSERT(count < ((uint64_t)(1) << 48));
        pos_and_val[i].count_and_idx = (count << 16) | i;
      }

      // Stable-sort -> elements with same number of zeros will preserve their
      // order.
      jxl_pos_and_count_sort(pos_and_val, pos_and_val + sz);

      // Grab indices.
      for (size_t i = 0; i < sz; ++i) {
        order[offset + i] = pos_and_val[i].pos;
        is_nondefault |= *jxl_array_at(natural_order_buffer, i) != pos_and_val[i].pos;
      }
    }
    if (!is_nondefault) {
      current_used_orders &= ~(1 << ord);
    }
  }
  *all_used_orders |= current_used_orders;
  jxl_aligned_memory_destroy(&mem);
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_compute_coeff_order(jxl_context* ctx,
                         jxl_speed_tier speed, const jxl_ac_image* ac_image,
                         const jxl_ac_strategy_image* ac_strategy,
                         const jxl_frame_dimensions* frame_dim,
                         uint32_t* all_used_orders, uint32_t prev_used_acs,
                         uint32_t current_used_acs,
                         uint32_t current_used_orders,
                         coeff_order_t* JXL_RESTRICT order){
  jxl_array_i64 num_zeros;
  jxl_array_construct_empty(&num_zeros, ctx);
  jxl_array_u32 natural_order_buffer;
  jxl_array_construct_empty(&natural_order_buffer, ctx);
  jxl_enc_status status = jxl_compute_coeff_order_body(
      speed, ac_image, ac_strategy, frame_dim, all_used_orders, prev_used_acs,
      current_used_acs, current_used_orders, order, &num_zeros,
      &natural_order_buffer);
  jxl_array_destroy(&num_zeros);
  jxl_array_destroy(&natural_order_buffer);
  return status;
}

static jxl_enc_status jxl_tokenize_permutation_body(const coeff_order_t* JXL_RESTRICT order,
                               size_t skip, size_t size, jxl_token_stream* tokens,
                               jxl_array_u32* lehmer, jxl_array_u32* temp) {
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(lehmer, size));
  JXL_RETURN_IF_ERROR(jxl_array_resize_zero(temp, size + 1));
  JXL_RETURN_IF_ERROR(
      jxl_compute_lehmer_code(order, jxl_array_data(temp), size, jxl_array_data(lehmer)));
  size_t end = size;
  while (end > skip && *jxl_array_at(lehmer, end - 1) == 0) {
    --end;
  }
  if (!jxl_enc_status_ok(jxl_array_token_push_back(tokens, jxl_token_make(jxl_coeff_order_context(size), (uint32_t)(end - skip))))) {
    JXL_CRASH();
  }
  uint32_t last = 0;
  for (size_t i = skip; i < end; ++i) {
if (!jxl_enc_status_ok(jxl_array_token_push_back(tokens, jxl_token_make(jxl_coeff_order_context(last), *jxl_array_at(lehmer, i))))) JXL_CRASH();
last = *jxl_array_at(lehmer, i);
  }
  return jxl_enc_ok_status();
}

static jxl_enc_status jxl_tokenize_permutation(const coeff_order_t* JXL_RESTRICT order, size_t skip,
                           size_t size, jxl_token_stream* tokens) {
  jxl_context* mm = tokens->ctx;
  jxl_array_u32 lehmer;
  jxl_array_construct_empty(&lehmer, mm);
  jxl_array_u32 temp;
  jxl_array_construct_empty(&temp, mm);
  jxl_enc_status status = jxl_tokenize_permutation_body(order, skip, size, tokens, &lehmer,
                                          &temp);
  jxl_array_destroy(&lehmer);
  jxl_array_destroy(&temp);
  return status;
}

static jxl_enc_status jxl_encode_coeff_order(const coeff_order_t* JXL_RESTRICT order, jxl_ac_strategy acs,
                        jxl_token_stream* tokens, coeff_order_t* order_zigzag,
                        jxl_array_u32* natural_order_lut) {
  const size_t llf = jxl_ac_strategy_covered_blocks_x(acs) * jxl_ac_strategy_covered_blocks_y(acs);
  const size_t size = kDCTBlockSize * llf;
  for (size_t i = 0; i < size; ++i) {
    order_zigzag[i] = *jxl_array_at(natural_order_lut, order[i]);
  }
  JXL_RETURN_IF_ERROR(jxl_tokenize_permutation(order_zigzag, llf, size, tokens));
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_encode_coeff_orders_body_inner(uint16_t used_orders,
                                  const coeff_order_t* JXL_RESTRICT order,
                                  jxl_bit_writer* writer, jxl_layer_type layer,
                                  jxl_aligned_memory* mem, jxl_token_streams* tokens,
                                  jxl_array_u32* natural_order_lut) {
  uint16_t computed = 0;
  for (uint8_t o = 0; o < kAcStrategyNumValidStrategies; ++o) {
    uint8_t ord = kStrategyOrder[o];
    if (computed & (1 << ord)) continue;
    computed |= 1 << ord;
    if ((used_orders & (1 << ord)) == 0) continue;
    jxl_ac_strategy acs = jxl_ac_strategy_from_raw_strategy_u8(o);
    const size_t llf = jxl_ac_strategy_covered_blocks_x(acs) * jxl_ac_strategy_covered_blocks_y(acs);
    const size_t size = kDCTBlockSize * llf;
    if (jxl_array_len(natural_order_lut) < size) {
      JXL_RETURN_IF_ERROR(jxl_array_resize_zero(natural_order_lut, size));
    }
    jxl_ac_strategy_compute_natural_coeff_order_lut(acs, jxl_array_data(natural_order_lut));
    for (size_t c = 0; c < 3; c++) {
      JXL_RETURN_IF_ERROR(
          jxl_encode_coeff_order(&order[jxl_coeff_order_offset(ord, c)], acs, jxl_token_streams_data(tokens),
                           (coeff_order_t*)(jxl_aligned_memory_address(mem)),
                           natural_order_lut));
    }
  }
  // Do not write anything if no order is used.
  if (used_orders != 0) {
    jxl_context* ctx = jxl_bit_writer_ctx(writer);
    jxl_entropy_encoding_data codes;
    jxl_entropy_encoding_data_init(&codes, ctx);
    jxl_histogram_params hist_params;
    jxl_histogram_params_construct_empty(&hist_params);
    jxl_array_size empty_widths;
    jxl_array_construct_empty(&empty_widths, ctx);
    size_t cost;
    jxl_enc_status status = jxl_build_and_encode_histograms(
        ctx, &hist_params, kPermutationContexts, tokens, &codes,
        writer, layer, &empty_widths, &cost);
    (void)cost;
    if (jxl_enc_status_ok(status)) {
      status = jxl_write_tokens(jxl_token_streams_at(tokens, 0), &codes, 0, writer, layer);
    }
    jxl_array_destroy(&empty_widths);
    jxl_entropy_encoding_data_destroy(&codes);
    JXL_RETURN_IF_ERROR(status);
  }
  return jxl_enc_ok_status();
}

jxl_enc_status jxl_encode_coeff_orders_body(uint16_t used_orders,
                             const coeff_order_t* JXL_RESTRICT order,
                             jxl_bit_writer* writer, jxl_layer_type layer,
                             jxl_aligned_memory* mem) {
  jxl_context* ctx = jxl_bit_writer_ctx(writer);
  jxl_token_streams tokens;
  jxl_token_streams_construct_empty(&tokens);
  tokens.ctx = ctx;
  if (!jxl_enc_status_ok(jxl_token_streams_resize(&tokens, 1))) JXL_CRASH();
  jxl_array_u32 natural_order_lut;
  jxl_array_construct_empty(&natural_order_lut, ctx);
  jxl_enc_status status = jxl_encode_coeff_orders_body_inner(used_orders, order, writer, layer,
                                             mem, &tokens, &natural_order_lut);
  jxl_array_destroy(&natural_order_lut);
  jxl_token_streams_destroy(&tokens);
  return status;
}

jxl_enc_status jxl_encode_coeff_orders(uint16_t used_orders,
                         const coeff_order_t* JXL_RESTRICT order,
                         jxl_bit_writer* writer, jxl_layer_type layer) {
  jxl_context* ctx = jxl_bit_writer_ctx(writer);
  size_t mem_bytes = kAcStrategyMaxCoeffArea * sizeof(coeff_order_t);
  jxl_aligned_memory mem;
  jxl_aligned_memory_construct_empty(&mem);
  jxl_enc_status status = jxl_aligned_memory_create(ctx, mem_bytes, 0, &mem);
  if (!jxl_enc_status_ok(status)) {
    jxl_aligned_memory_destroy(&mem);
    return status;
  }
  status = jxl_encode_coeff_orders_body(used_orders, order, writer, layer, &mem);
  jxl_aligned_memory_destroy(&mem);
  return status;
}
