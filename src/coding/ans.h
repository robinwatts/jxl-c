// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_ANS_H_
#define JXL_CODING_ANS_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "coding/error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

/* Ported from libjxl; repr(C) layout must be 8 bytes. */
typedef struct jxl_ans_bucket {
    uint8_t alias_symbol;
    uint8_t alias_cutoff;
    uint16_t dist;
    uint16_t alias_offset;
    uint16_t alias_dist_xor;
} jxl_ans_bucket;

typedef struct jxl_ans_histogram {
    jxl_ans_bucket *buckets;
    size_t bucket_count;
    uint32_t log_bucket_size;
    uint32_t bucket_mask;
    int has_single_symbol;
    uint32_t single_symbol;
} jxl_ans_histogram;

jxl_coding_status_t jxl_ans_histogram_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                            uint32_t log_alphabet_size, jxl_ans_histogram *out);

void jxl_ans_histogram_destroy(jxl_allocator_state *alloc, jxl_ans_histogram *hist);

jxl_coding_status_t jxl_ans_histogram_clone(jxl_allocator_state *alloc,
                                            const jxl_ans_histogram *src,
                                            jxl_ans_histogram *out);

jxl_coding_status_t jxl_ans_histogram_read_symbol(const jxl_ans_histogram *hist, jxl_bs *bs,
                                                  uint32_t *state, uint32_t *symbol_out);

/* Returns 1 if histogram encodes a single symbol, 0 otherwise. */
int jxl_ans_histogram_single_symbol(const jxl_ans_histogram *hist, uint32_t *symbol_out);

#endif /* JXL_CODING_ANS_H_ */
