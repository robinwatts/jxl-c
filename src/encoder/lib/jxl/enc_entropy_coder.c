// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "lib/jxl/enc_entropy_coder.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/bits.h"
#include "lib/jxl/base/enc_status.h"
#include "lib/jxl/coeff_order.h"
#include "lib/jxl/entropy_coder.h"
#include "lib/jxl/pack_signed.h"


// Returns number of non-zero coefficients (but skip LLF).
// We cannot rely on block[] being all-zero bits, so first truncate to integer.
// Also writes the per-8x8 block nzeros starting at nzeros_pos.
int32_t jxl_num_non_zero_except_llf(const size_t cx, const size_t cy,
                            const jxl_ac_strategy acs, const size_t covered_blocks,
                            const size_t log2_covered_blocks,
                            const int32_t* JXL_RESTRICT block,
                            const size_t nzeros_stride,
                            int32_t* JXL_RESTRICT nzeros_pos) {
  int32_t nzeros = 0;
  for (size_t y = 0; y < cy * kBlockDim; y++) {
    for (size_t x = 0; x < cx * kBlockDim; x++) {
      const bool is_llf = y < cy && x < cx;
      if (!is_llf && block[y * cx * kBlockDim + x] != 0) {
        nzeros++;
      }
    }
  }

  const int32_t shifted_nzeros = (int32_t)(
      (nzeros + covered_blocks - 1) >> log2_covered_blocks);
  // Need non-canonicalized dimensions!
  for (size_t y = 0; y < jxl_ac_strategy_covered_blocks_y(acs); y++) {
    for (size_t x = 0; x < jxl_ac_strategy_covered_blocks_x(acs); x++) {
      nzeros_pos[x + y * nzeros_stride] = shifted_nzeros;
    }
  }

  return nzeros;
}

// Specialization for 8x8, where only top-left is LLF/DC.
int32_t jxl_num_non_zero8x8_except_dc(const int32_t* JXL_RESTRICT block,
                              int32_t* JXL_RESTRICT nzeros_pos) {
  int32_t nzeros = 0;
  for (size_t y = 0; y < kBlockDim; y++) {
    for (size_t x = 0; x < kBlockDim; x++) {
      const bool is_dc = y == 0 && x == 0;
      if (!is_dc && block[y * kBlockDim + x] != 0) {
        nzeros++;
      }
    }
  }

  *nzeros_pos = nzeros;

  return nzeros;
}

// The number of nonzeros of each block is predicted from the top and the left
// blocks, with opportune scaling to take into account the number of blocks of
// each strategy.  The predicted number of nonzeros divided by two is used as a
// context; if this number is above 63, a specific context is used.  If the
// number of nonzeros of a strategy is above 63, it is written directly using a
// fixed number of bits (that depends on the size of the strategy).
jxl_status jxl_tokenize_coefficients(const coeff_order_t* JXL_RESTRICT orders,
                            const jxl_rect* rect,
                            const int32_t* JXL_RESTRICT* JXL_RESTRICT ac_rows,
                            const jxl_ac_strategy_image* ac_strategy,
                            const jxl_y_cb_cr_chroma_subsampling* cs,
                            jxl_image3_i* JXL_RESTRICT tmp_num_nzeroes,
                            jxl_token_stream* JXL_RESTRICT output,
                            const jxl_image_b* qdc, const jxl_image_i* qf,
                            const jxl_block_ctx_map* block_ctx_map){
  const size_t xsize_blocks = jxl_rect_x_size(rect);
  const size_t ysize_blocks = jxl_rect_y_size(rect);
jxl_array_clear(output);
  // TODO(user): update the estimate: usually less coefficients are used.
if (!jxl_status_ok(jxl_array_reserve(output, 3 * xsize_blocks * ysize_blocks * kDCTBlockSize))) JXL_CRASH();
  size_t offset[3];
  memset(offset, 0, sizeof(offset));
  const size_t nzeros_stride = jxl_image3_i_pixels_per_row(tmp_num_nzeroes);
  for (size_t by = 0; by < ysize_blocks; ++by) {
    size_t sby[3] = {by >> jxl_y_cb_cr_chroma_subsampling_v_shift(cs, 0), by >> jxl_y_cb_cr_chroma_subsampling_v_shift(cs, 1),
                     by >> jxl_y_cb_cr_chroma_subsampling_v_shift(cs, 2)};
    int32_t* JXL_RESTRICT row_nzeros[3] = {
        jxl_image3_i_plane_row(tmp_num_nzeroes, 0, sby[0]),
        jxl_image3_i_plane_row(tmp_num_nzeroes, 1, sby[1]),
        jxl_image3_i_plane_row(tmp_num_nzeroes, 2, sby[2]),
    };
    const int32_t* JXL_RESTRICT row_nzeros_top[3] = {
        sby[0] == 0 ? NULL : jxl_image3_i_const_plane_row(tmp_num_nzeroes, 0, sby[0] - 1),
        sby[1] == 0 ? NULL : jxl_image3_i_const_plane_row(tmp_num_nzeroes, 1, sby[1] - 1),
        sby[2] == 0 ? NULL : jxl_image3_i_const_plane_row(tmp_num_nzeroes, 2, sby[2] - 1),
    };
    const uint8_t* JXL_RESTRICT row_qdc =
        jxl_image_b_const_row(qdc, jxl_rect_y0(rect) + by) + jxl_rect_x0(rect);
    const int32_t* JXL_RESTRICT row_qf = jxl_rect_const_row_i(rect, qf, by);
    jxl_ac_strategy_row acs_row = jxl_ac_strategy_image_const_row_rect(ac_strategy, rect, by);
    for (size_t bx = 0; bx < xsize_blocks; ++bx) {
      jxl_ac_strategy acs = jxl_ac_strategy_row_at(acs_row, bx);
      if (!jxl_ac_strategy_is_first_block(acs)) continue;
      size_t sbx[3] = {bx >> jxl_y_cb_cr_chroma_subsampling_h_shift(cs, 0), bx >> jxl_y_cb_cr_chroma_subsampling_h_shift(cs, 1),
                       bx >> jxl_y_cb_cr_chroma_subsampling_h_shift(cs, 2)};
      size_t cx = jxl_ac_strategy_covered_blocks_x(acs);
      size_t cy = jxl_ac_strategy_covered_blocks_y(acs);
      const size_t covered_blocks = cx * cy;  // = #LLF coefficients
      const size_t log2_covered_blocks =
          Num0BitsBelowLS1Bit_Nonzero32(covered_blocks);
      const size_t size = covered_blocks * kDCTBlockSize;

      jxl_coefficient_layout(&cy, &cx);  // swap cx/cy to canonical order

      static const int kChans[] = {1, 0, 2};
      for (size_t c_i = 0; c_i < 3; ++c_i) {
        int c = kChans[c_i];
        if (sbx[c] << jxl_y_cb_cr_chroma_subsampling_h_shift(cs, c) != bx) continue;
        if (sby[c] << jxl_y_cb_cr_chroma_subsampling_v_shift(cs, c) != by) continue;
        const int32_t* JXL_RESTRICT block = ac_rows[c] + offset[c];

        int32_t nzeros =
            (covered_blocks == 1)
                ? jxl_num_non_zero8x8_except_dc(block, row_nzeros[c] + sbx[c])
                : jxl_num_non_zero_except_llf(cx, cy, acs, covered_blocks,
                                      log2_covered_blocks, block, nzeros_stride,
                                      row_nzeros[c] + sbx[c]);

        int ord = kStrategyOrder[jxl_ac_strategy_raw_strategy(acs)];
        const coeff_order_t* JXL_RESTRICT order =
            &orders[jxl_coeff_order_offset(ord, c)];

        int32_t predicted_nzeros =
            jxl_predict_from_top_and_left(row_nzeros_top[c], row_nzeros[c], sbx[c], 32);
        size_t block_ctx =
            jxl_block_ctx_map_context(block_ctx_map, row_qdc[bx], row_qf[sbx[c]], ord, c);
        const int32_t nzero_ctx =
            jxl_block_ctx_map_non_zero_context(block_ctx_map, predicted_nzeros, block_ctx);

if (!jxl_status_ok(jxl_array_token_push_back(output, jxl_token_make(nzero_ctx, nzeros)))) JXL_CRASH();
const size_t histo_offset =
            jxl_block_ctx_map_zero_density_contexts_offset(block_ctx_map, block_ctx);
        // Skip LLF.
        size_t prev = (nzeros > (ptrdiff_t)(size / 16) ? 0 : 1);
        for (size_t k = covered_blocks; k < size && nzeros != 0; ++k) {
          int32_t coeff = block[order[k]];
          size_t ctx =
              histo_offset + jxl_zero_density_context(nzeros, k, covered_blocks,
                                                log2_covered_blocks, prev);
          uint32_t u_coeff = jxl_pack_signed(coeff);
if (!jxl_status_ok(jxl_array_token_push_back(output, jxl_token_make((uint32_t)(ctx), u_coeff)))) JXL_CRASH();
prev = (coeff != 0) ? 1 : 0;
          nzeros -= prev;
        }
        JXL_ENSURE(nzeros == 0);
        offset[c] += size;
      }
    }
  }
  return jxl_ok_status();
}
