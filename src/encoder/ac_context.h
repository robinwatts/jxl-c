// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in LICENSE-BSD.

#ifndef JXL_ENC_AC_CONTEXT_H_
#define JXL_ENC_AC_CONTEXT_H_

#include <stddef.h>
#include <stdint.h>

#include "base/array.h"
#include "base/compiler_specific.h"
#include "base/enc_status.h"
#include "coeff_order_fwd.h"

// Block context used for scanning order, number of non-zeros, AC coefficients.
// Equal to the channel.

// The number of predicted nonzeros goes from 0 to 1008. We use
// ceil(log2(predicted+1)) as a context for the number of nonzeros, so from 0 to
// 10, inclusive.
enum { kNonZeroBuckets = 37 };

static const uint16_t kCoeffFreqContext[64] = {
    0xBAD, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
    15,    15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
    23,    23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26,
    27,    27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30,
};

static const uint16_t kCoeffNumNonzeroContext[64] = {
    0xBAD, 0,   31,  62,  62,  93,  93,  93,  93,  123, 123, 123, 123,
    152,   152, 152, 152, 152, 152, 152, 152, 180, 180, 180, 180, 180,
    180,   180, 180, 180, 180, 180, 180, 206, 206, 206, 206, 206, 206,
    206,   206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206,
    206,   206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206,
};

// Supremum of jxl_zero_density_context(x, y) + 1, when x + y < 64.
enum { kZeroDensityContextCount = 458 };

/* This function is used for entropy-sources pre-clustering.
 *
 * Ideally, each combination of |nonzeros_left| and |k| should go to its own
 * bucket; but it implies (64 * 63 / 2) == 2016 buckets. If there is other
 * dimension (e.g. block context), then number of primary clusters becomes too
 * big.
 *
 * To solve this problem, |nonzeros_left| and |k| values are clustered. It is
 * known that their sum is at most 64, consequently, the total number buckets
 * is at most A(64) * B(64).
 */
// TODO(user): investigate, why disabling pre-clustering makes entropy code
// less dense. Perhaps we would need to add HQ clustering algorithm that would
// be able to squeeze better by spending more CPU cycles.
static JXL_INLINE size_t jxl_zero_density_context(size_t nonzeros_left, size_t k,
                                            size_t covered_blocks,
                                            size_t log2_covered_blocks,
                                            size_t prev) {
  JXL_DASSERT(((size_t)(1) << log2_covered_blocks) ==
              covered_blocks);
  nonzeros_left = (nonzeros_left + covered_blocks - 1) >> log2_covered_blocks;
  k >>= log2_covered_blocks;
  JXL_DASSERT(k > 0);
  JXL_DASSERT(k < 64);
  JXL_DASSERT(nonzeros_left > 0);
  // Asserting nonzeros_left + k < 65 here causes crashes in debug mode with
  // invalid input, since the (hot) decoding loop does not check this condition.
  // As no out-of-bound memory reads are issued even if that condition is
  // broken, we check this simpler condition which holds anyway. The decoder
  // will still mark a file in which that condition happens as not valid at the
  // end of the decoding loop, as `nzeros` will not be `0`.
  JXL_DASSERT(nonzeros_left < 64);
  return (kCoeffNumNonzeroContext[nonzeros_left] + kCoeffFreqContext[k]) * 2 +
         prev;
}

static const uint8_t kBlockCtxMapDefault[3 * kNumOrders] = {
    // Default ctx map clusters all the large transforms together.
    0, 1, 2, 2, 3,  3,  4,  5,  6,  6,  6,  6,  6,   //
    7, 8, 9, 9, 10, 11, 12, 13, 14, 14, 14, 14, 14,  //
    7, 8, 9, 9, 10, 11, 12, 13, 14, 14, 14, 14, 14,  //
};
JXL_STATIC_ASSERT(3 * kNumOrders ==
                  sizeof(kBlockCtxMapDefault) / sizeof *kBlockCtxMapDefault,
                  "Update default context map");

typedef struct jxl_block_ctx_map {
  jxl_array_int dc_thresholds[3];
  jxl_array_u32 qf_thresholds;
  jxl_array_u8 ctx_map;
  size_t num_ctxs, num_dc_ctxs;
} jxl_block_ctx_map;

static inline void jxl_block_ctx_map_init(jxl_block_ctx_map* self,
                                          jxl_context* mm) {
  for (size_t i = 0; i < 3; ++i) {
    jxl_array_construct_empty(&self->dc_thresholds[i], mm);
  }
  jxl_array_construct_empty(&self->qf_thresholds, mm);
  jxl_array_construct_empty(&self->ctx_map, mm);
  if (!jxl_enc_status_ok(jxl_array_assign(&self->ctx_map, kBlockCtxMapDefault,
                   sizeof(kBlockCtxMapDefault) / sizeof(kBlockCtxMapDefault[0])))) {
    JXL_CRASH();
  }
  self->num_ctxs = jxl_u8_max_element(&self->ctx_map) + 1;
  self->num_dc_ctxs = 1;
}

static inline void jxl_block_ctx_map_destroy(jxl_block_ctx_map* self) {
  for (size_t i = 0; i < 3; ++i) {
    jxl_array_destroy(&self->dc_thresholds[i]);
  }
  jxl_array_destroy(&self->qf_thresholds);
  jxl_array_destroy(&self->ctx_map);
  self->num_ctxs = 0;
  self->num_dc_ctxs = 0;
}

static inline size_t jxl_block_ctx_map_context(const jxl_block_ctx_map* self, int dc_idx,
                                 uint32_t qf, size_t ord, size_t c) {
  size_t qf_idx = 0;
  for (size_t t_i = 0; t_i < jxl_array_len(&self->qf_thresholds); ++t_i) {
    uint32_t t = *jxl_array_at_const(&self->qf_thresholds, t_i);
    if (qf > t) qf_idx++;
  }
  size_t idx = c < 2 ? c ^ 1 : 2;
  idx = idx * kNumOrders + ord;
  idx = idx * (jxl_array_len(&self->qf_thresholds) + 1) + qf_idx;
  idx = idx * self->num_dc_ctxs + dc_idx;
  return *jxl_array_at_const(&self->ctx_map, idx);
}

// Non-zero context is based on number of non-zeros and block context.
// For better clustering, contexts with same number of non-zeros are grouped.
static inline uint32_t jxl_block_ctx_map_zero_density_contexts_offset(const jxl_block_ctx_map* self,
                                                     uint32_t block_ctx) {
  return (uint32_t)(self->num_ctxs * kNonZeroBuckets +
                    kZeroDensityContextCount * block_ctx);
}

// Context map for AC coefficients consists of 2 blocks:
//  |num_ctxs x                : context for number of non-zeros in the block
//   kNonZeroBuckets|            computed from block context and predicted
//                               value (based top and left values)
//  |num_ctxs x                : context for AC coefficient symbols,
//   kZeroDensityContextCount|   computed from block context,
//                               number of non-zeros left and
//                               index in scan order
static inline uint32_t jxl_block_ctx_map_num_ac_contexts(const jxl_block_ctx_map* self) {
  return (uint32_t)(self->num_ctxs *
                    (kNonZeroBuckets + kZeroDensityContextCount));
}

// Non-zero context is based on number of non-zeros and block context.
// For better clustering, contexts with same number of non-zeros are grouped.
static inline uint32_t jxl_block_ctx_map_non_zero_context(const jxl_block_ctx_map* self,
                                          uint32_t non_zeros,
                                          uint32_t block_ctx) {
  uint32_t ctx;
  if (non_zeros >= 64) non_zeros = 64;
  if (non_zeros < 8) {
    ctx = non_zeros;
  } else {
    ctx = 4 + non_zeros / 2;
  }
  return (uint32_t)(ctx * self->num_ctxs + block_ctx);
}




#endif  // JXL_ENC_AC_CONTEXT_H_
