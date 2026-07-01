// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef JXL_CODING_PREFIX_H_
#define JXL_CODING_PREFIX_H_

#include "allocator.h"
#include "bitstream/bitstream.h"
#include "coding/error.h"

#include <stddef.h>
#include "jxl_oxide/jxl_types.h"

#define JXL_PREFIX_MAX_PREFIX_BITS 15
#define JXL_PREFIX_MAX_TOPLEVEL_BITS 10

typedef struct jxl_prefix_entry {
    uint8_t nested;
    uint8_t bits_or_mask;
    uint16_t symbol_or_offset;
} jxl_prefix_entry;

typedef struct jxl_prefix_histogram {
    size_t toplevel_bits;
    uint32_t toplevel_mask;
    jxl_prefix_entry *toplevel_entries;
    size_t toplevel_len;
    jxl_prefix_entry *second_level_entries;
    size_t second_level_len;
} jxl_prefix_histogram;

jxl_coding_status_t jxl_prefix_histogram_parse(jxl_allocator_state *alloc, jxl_bs *bs,
                                               uint32_t alphabet_size,
                                               jxl_prefix_histogram *out);

void jxl_prefix_histogram_destroy(jxl_allocator_state *alloc, jxl_prefix_histogram *hist);

jxl_coding_status_t jxl_prefix_histogram_read_symbol(const jxl_prefix_histogram *hist, jxl_bs *bs,
                                                     uint32_t *symbol_out);

int jxl_prefix_histogram_single_symbol(const jxl_prefix_histogram *hist, uint32_t *symbol_out);

#endif /* JXL_CODING_PREFIX_H_ */
